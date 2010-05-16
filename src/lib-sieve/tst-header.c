/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-match.h"

/* 
 * Header test 
 *
 * Syntax:
 *   header [COMPARATOR] [MATCH-TYPE]
 *     <header-names: string-list> <key-list: string-list>
 */

static bool tst_header_registered
	(struct sieve_validator *valdtr, const struct sieve_extension *ext,
		struct sieve_command_registration *cmd_reg);
static bool tst_header_validate
	(struct sieve_validator *valdtr, struct sieve_command *tst);
static bool tst_header_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *tst);

const struct sieve_command_def tst_header = { 
	"header", 
	SCT_TEST,
	2, 0, FALSE, FALSE,
	tst_header_registered, 
	NULL,
	tst_header_validate, 
	tst_header_generate, 
	NULL 
};

/* 
 * Header operation 
 */

static bool tst_header_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_header_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def tst_header_operation = { 
	"HEADER",
	NULL,
	SIEVE_OPERATION_HEADER,
	tst_header_operation_dump, 
	tst_header_operation_execute 
};

/* 
 * Test registration 
 */

static bool tst_header_registered
(struct sieve_validator *valdtr, const struct sieve_extension *ext ATTR_UNUSED, 
	struct sieve_command_registration *cmd_reg) 
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(valdtr, cmd_reg, SIEVE_MATCH_OPT_COMPARATOR);
	sieve_match_types_link_tags(valdtr, cmd_reg, SIEVE_MATCH_OPT_MATCH_TYPE);

	return TRUE;
}

/* 
 * Validation 
 */
 
static bool tst_header_validate
(struct sieve_validator *valdtr, struct sieve_command *tst) 
{ 		
	struct sieve_ast_argument *arg = tst->first_positional;
	struct sieve_comparator cmp_default = 
		SIEVE_COMPARATOR_DEFAULT(i_ascii_casemap_comparator);
	struct sieve_match_type mcht_default = 
		SIEVE_COMPARATOR_DEFAULT(is_match_type);
	
	if ( !sieve_validate_positional_argument
		(valdtr, tst, arg, "header names", 1, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	
	if ( !sieve_validator_argument_activate(valdtr, tst, arg, FALSE) )
		return FALSE;

	if ( !sieve_command_verify_headers_argument(valdtr, arg) )
		return FALSE;
	
	arg = sieve_ast_argument_next(arg);

	if ( !sieve_validate_positional_argument
		(valdtr, tst, arg, "key list", 2, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	
	if ( !sieve_validator_argument_activate(valdtr, tst, arg, FALSE) )
		return FALSE;

	/* Validate the key argument to a specified match type */
	return sieve_match_type_validate
		(valdtr, tst, arg, &mcht_default, &cmp_default);
}

/*
 * Code generation 
 */

static bool tst_header_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *tst) 
{
	sieve_operation_emit(cgenv->sblock, NULL, &tst_header_operation);

 	/* Generate arguments */
	return sieve_generate_arguments(cgenv, tst, NULL);
}

/* 
 * Code dump 
 */

static bool tst_header_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	int opt_code = 0;

	sieve_code_dumpf(denv, "HEADER");
	sieve_code_descend(denv);

	/* Optional operands */
	if ( sieve_match_opr_optional_dump(denv, address, &opt_code) != 0 )
		return FALSE;
	
	return
		sieve_opr_stringlist_dump(denv, address, "header names") &&
		sieve_opr_stringlist_dump(denv, address, "key list");
}

/* 
 * Code execution 
 */

static inline string_t *_header_right_trim(const char *raw) 
{
	string_t *result;
	int i;
	
	for ( i = strlen(raw)-1; i >= 0; i-- ) {
		if ( raw[i] != ' ' && raw[i] != '\t' ) break;
	}
	
	result = t_str_new(i+1);
	str_append_n(result, raw, i + 1);
	return result;
}

static int tst_header_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	bool result = TRUE;
	int opt_code = 0;
	struct sieve_comparator cmp = 
		SIEVE_COMPARATOR_DEFAULT(i_ascii_casemap_comparator);
	struct sieve_match_type mcht = 
		SIEVE_COMPARATOR_DEFAULT(is_match_type);
	struct sieve_match_context *mctx;
	struct sieve_coded_stringlist *hdr_list;
	struct sieve_coded_stringlist *key_list;
	string_t *hdr_item;
	bool matched;
	int ret;
	
	/* 
	 * Read operands
	 */

	/* Handle match-type and comparator operands */
	if ( (ret=sieve_match_opr_optional_read
		(renv, address, &opt_code, &cmp, &mcht)) < 0 )
		return SIEVE_EXEC_BIN_CORRUPT;

	/* Check whether we neatly finished the list of optional operands*/
	if ( ret > 0 ) {
		sieve_runtime_trace_error(renv, "invalid optional operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
		
	/* Read header-list */
	if ( (hdr_list=sieve_opr_stringlist_read(renv, address, "header-list")) 
		== NULL )
		return SIEVE_EXEC_BIN_CORRUPT;
	
	/* Read key-list */
	if ( (key_list=sieve_opr_stringlist_read(renv, address, "key-list"))
		== NULL )
		return SIEVE_EXEC_BIN_CORRUPT;

	/*
	 * Perform test
	 */

	sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS, "header test");

	/* Initialize match */
	mctx = sieve_match_begin(renv->interp, &mcht, &cmp, NULL, key_list); 	

	/* Iterate through all requested headers to match */
	hdr_item = NULL;
	matched = FALSE;
	while ( result && !matched && 
		(result=sieve_coded_stringlist_next_item(hdr_list, &hdr_item)) 
		&& hdr_item != NULL ) {
		const char *const *headers;
			
		if ( mail_get_headers_utf8
			(renv->msgdata->mail, str_c(hdr_item), &headers) >= 0 ) {	
			int i;

			for ( i = 0; !matched && headers[i] != NULL; i++ ) {
				string_t *theader = _header_right_trim(headers[i]);
			
				if ( (ret=sieve_match_value(mctx, str_c(theader), str_len(theader))) 
					< 0 ) 
				{
					result = FALSE;
					break;
				}

				matched = ret > 0;				
			} 
		}
	}

	/* Finish match */
	if ( (ret=sieve_match_end(&mctx)) < 0 ) 
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
