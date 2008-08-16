/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "str-sanitize.h"

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-code.h"

#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-address-parts.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-match.h"

#include <stdio.h>

/* 
 * Address test
 *
 * Syntax:
 *    address [ADDRESS-PART] [COMPARATOR] [MATCH-TYPE]
 *       <header-list: string-list> <key-list: string-list>
 */

static bool tst_address_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool tst_address_validate
	(struct sieve_validator *validator, struct sieve_command_context *tst);
static bool tst_address_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

const struct sieve_command tst_address = { 
	"address", 
	SCT_TEST, 
	2, 0, FALSE, FALSE,
	tst_address_registered,
	NULL, 
	tst_address_validate, 
	tst_address_generate, 
	NULL 
};

/* 
 * Address operation 
 */

static bool tst_address_operation_dump
	(const struct sieve_operation *op, 
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_address_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation tst_address_operation = { 
	"ADDRESS",
	NULL,
	SIEVE_OPERATION_ADDRESS,
	tst_address_operation_dump, 
	tst_address_operation_execute 
};

/* 
 * Test registration 
 */

static bool tst_address_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(validator, cmd_reg, SIEVE_AM_OPT_COMPARATOR );
	sieve_address_parts_link_tags(validator, cmd_reg, SIEVE_AM_OPT_ADDRESS_PART);
	sieve_match_types_link_tags(validator, cmd_reg, SIEVE_AM_OPT_MATCH_TYPE);

	return TRUE;
}

/* 
 * Validation 
 */
 
/* List of valid headers:
 *   Implementations MUST restrict the address test to headers that
 *   contain addresses, but MUST include at least From, To, Cc, Bcc,
 *   Sender, Resent-From, and Resent-To, and it SHOULD include any other
 *   header that utilizes an "address-list" structured header body.
 *
 * This list explicitly does not contain the envelope-to and return-path 
 * headers. The envelope test must be used to test against these addresses.
 */
static const char * const _allowed_headers[] = {
	/* Required */
	"from", "to", "cc", "bcc", "sender", "resent-from", "resent-to",

	/* Additional (RFC 2822) */
	"reply-to", "resent-reply-to", 
	
	/* Non-standard (draft-palme-mailext-headers-08.txt) */
	"for-approval", "for-handling", "for-comment", "apparently-to", "errors-to", 
	"delivered-to", "return-receipt-to", "x-admin", "read-receipt-to", 
	"x-confirm-reading-to", "return-receipt-requested", 
	"registered-mail-reply-requested-by", "mail-followup-to", "mail-reply-to",
	"abuse-reports-to", "x-complaints-to", "x-report-abuse-to",
	
	NULL  
};

static int _header_is_allowed
(void *context ATTR_UNUSED, struct sieve_ast_argument *arg)
{
	if ( sieve_argument_is_string_literal(arg) ) {
		const char *header = sieve_ast_strlist_strc(arg);

		const char * const *hdsp = _allowed_headers;
		while ( *hdsp != NULL ) {
			if ( strcasecmp( *hdsp, header ) == 0 ) 
				return TRUE;

			hdsp++;
		}
		
		return FALSE;
	}
	
	return TRUE;
}

static bool tst_address_validate
	(struct sieve_validator *validator, struct sieve_command_context *tst) 
{
	struct sieve_ast_argument *arg = tst->first_positional;
	struct sieve_ast_argument *header;
		
	if ( !sieve_validate_positional_argument
		(validator, tst, arg, "header list", 1, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	
	if ( !sieve_validator_argument_activate(validator, tst, arg, FALSE) )
		return FALSE;

	/* Check if supplied header names are allowed
	 *   FIXME: verify dynamic header names at runtime 
	 */
	header = arg;
	if ( !sieve_ast_stringlist_map(&header, NULL, _header_is_allowed) ) {		
		sieve_command_validate_error(validator, tst, 
			"specified header '%s' is not allowed for the address test", 
			str_sanitize(sieve_ast_strlist_strc(header), 64));
		return FALSE;
	}

	/* Check key list */
	
	arg = sieve_ast_argument_next(arg);
	
	if ( !sieve_validate_positional_argument
		(validator, tst, arg, "key list", 2, SAAT_STRING_LIST) ) {
		return FALSE;
	}

	if ( !sieve_validator_argument_activate(validator, tst, arg, FALSE) )
		return FALSE;
	
	/* Validate the key argument to a specified match type */
	return sieve_match_type_validate
		(validator, tst, arg, &is_match_type, &i_ascii_casemap_comparator); 
}

/* 
 * Code generation 
 */

static bool tst_address_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx) 
{
	sieve_operation_emit_code(cgenv->sbin, &tst_address_operation);
	
	/* Generate arguments */  	
	return sieve_generate_arguments(cgenv, ctx, NULL);
}

/* 
 * Code dump 
 */

static bool tst_address_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,	
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "ADDRESS");
	sieve_code_descend(denv);
	
	/* Handle any optional arguments */
	if ( !sieve_addrmatch_default_dump_optionals(denv, address) )
		return FALSE;

	return
		sieve_opr_stringlist_dump(denv, address) &&
		sieve_opr_stringlist_dump(denv, address);
}

/* 
 * Code execution 
 */

static int tst_address_operation_execute
(const struct sieve_operation *op ATTR_UNUSED, 
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{	
	bool result = TRUE;
	const struct sieve_comparator *cmp = &i_ascii_casemap_comparator;
	const struct sieve_match_type *mtch = &is_match_type;
	const struct sieve_address_part *addrp = &all_address_part;
	struct sieve_match_context *mctx;
	struct sieve_coded_stringlist *hdr_list;
	struct sieve_coded_stringlist *key_list;
	string_t *hdr_item;
	bool matched;
	int ret;
	
	/* Read optional operands */
	if ( (ret=sieve_addrmatch_default_get_optionals
		(renv, address, &addrp, &mtch, &cmp)) <= 0 ) 
		return ret;
		
	/* Read header-list */
	if ( (hdr_list=sieve_opr_stringlist_read(renv, address)) == NULL ) {
		sieve_runtime_trace_error(renv, "invalid header-list operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/* Read key-list */
	if ( (key_list=sieve_opr_stringlist_read(renv, address)) == NULL ) {
		sieve_runtime_trace_error(renv, "invalid key-list operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	sieve_runtime_trace(renv, "ADDRESS test");

	/* Initialize match context */
	mctx = sieve_match_begin(renv->interp, mtch, cmp, NULL, key_list);
	
	/* Iterate through all requested headers to match */
	hdr_item = NULL;
	matched = FALSE;
	while ( result && !matched && 
		(result=sieve_coded_stringlist_next_item(hdr_list, &hdr_item)) 
		&& hdr_item != NULL ) {
		const char *const *headers;
			
		if ( mail_get_headers_utf8(renv->msgdata->mail, str_c(hdr_item), &headers) >= 0 ) {	
			int i;

			for ( i = 0; !matched && headers[i] != NULL; i++ ) {
				if ( (ret=sieve_address_match(addrp, mctx, headers[i])) < 0 ) {
					result = FALSE;
					break;
				}
				
				matched = ret > 0;				
			} 
		}
	}
	
	/* Finish match */

	if ( (ret=sieve_match_end(mctx)) < 0 )
		result = FALSE;
	else
		matched = ( ret > 0 || matched );
	
	/* Set test result for subsequent conditional jump */
	if ( result ) {
		sieve_interpreter_set_test_result(renv->interp, matched);
		return SIEVE_EXEC_OK;
	}

	sieve_runtime_trace_error(renv, "invalid string-list item");	
	return SIEVE_EXEC_BIN_CORRUPT;
}
