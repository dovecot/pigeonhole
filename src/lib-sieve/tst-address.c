#include <stdio.h>

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-code.h"

#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-address-parts.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code-dumper.h"

/* Address test
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
	(struct sieve_generator *generator, struct sieve_command_context *ctx);

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

/* Operands */

static bool tst_address_operation_dump
	(const struct sieve_operation *op, 
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static bool tst_address_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation tst_address_operation = { 
	"ADDRESS",
	NULL,
	SIEVE_OPERATION_ADDRESS,
	tst_address_operation_dump, 
	tst_address_operation_execute 
};

/* Test registration */

static bool tst_address_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(validator, cmd_reg, SIEVE_AM_OPT_COMPARATOR );
	sieve_address_parts_link_tags(validator, cmd_reg, SIEVE_AM_OPT_ADDRESS_PART);
	sieve_match_types_link_tags(validator, cmd_reg, SIEVE_AM_OPT_MATCH_TYPE);

	return TRUE;
}

/* Test validation */

static bool tst_address_validate
	(struct sieve_validator *validator, struct sieve_command_context *tst) 
{
	struct sieve_ast_argument *arg = tst->first_positional;
		
	if ( !sieve_validate_positional_argument
		(validator, tst, arg, "header list", 1, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	
	if ( !sieve_validator_argument_activate(validator, tst, arg, FALSE) )
		return FALSE;

	arg = sieve_ast_argument_next(arg);
	
	if ( !sieve_validate_positional_argument
		(validator, tst, arg, "key list", 2, SAAT_STRING_LIST) ) {
		return FALSE;
	}

	if ( !sieve_validator_argument_activate(validator, tst, arg, FALSE) )
		return FALSE;
	
	/* Validate the key argument to a specified match type */
	return sieve_match_type_validate(validator, tst, arg);
}

/* Test generation */

static bool tst_address_generate
	(struct sieve_generator *generator, struct sieve_command_context *ctx) 
{
	sieve_generator_emit_operation(generator, &tst_address_operation);
	
	/* Generate arguments */  	
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;
	
	return TRUE;
}

/* Code dump */

static bool tst_address_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,	
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "ADDRESS");
	sieve_code_descend(denv);
	
	//* Handle any optional arguments */
	if ( !sieve_addrmatch_default_dump_optionals(denv, address) )
		return FALSE;

	return
		sieve_opr_stringlist_dump(denv, address) &&
		sieve_opr_stringlist_dump(denv, address);
}

/* Code execution */

static bool tst_address_operation_execute
(const struct sieve_operation *op ATTR_UNUSED, 
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{	
	bool result = TRUE;
	const struct sieve_comparator *cmp = &i_octet_comparator;
	const struct sieve_match_type *mtch = &is_match_type;
	const struct sieve_address_part *addrp = &all_address_part;
	struct sieve_match_context *mctx;
	struct sieve_coded_stringlist *hdr_list;
	struct sieve_coded_stringlist *key_list;
	string_t *hdr_item;
	bool matched;
	
	printf("?? ADDRESS\n");

	if ( !sieve_addrmatch_default_get_optionals
		(renv, address, &addrp, &mtch, &cmp) )
		return FALSE; 

	t_push();
		
	/* Read header-list */
	if ( (hdr_list=sieve_opr_stringlist_read(renv, address)) == NULL ) {
		t_pop();
		return FALSE;
	}
	
	/* Read key-list */
	if ( (key_list=sieve_opr_stringlist_read(renv, address)) == NULL ) {
		t_pop();
		return FALSE;
	}

	/* Initialize match context */
	mctx = sieve_match_begin(renv->interp, mtch, cmp, key_list);
	
	/* Iterate through all requested headers to match */
	hdr_item = NULL;
	matched = FALSE;
	while ( !matched && (result=sieve_coded_stringlist_next_item(hdr_list, &hdr_item)) 
		&& hdr_item != NULL ) {
		const char *const *headers;
			
		if ( mail_get_headers_utf8(renv->msgdata->mail, str_c(hdr_item), &headers) >= 0 ) {	
			
			int i;
			for ( i = 0; !matched && headers[i] != NULL; i++ ) {
				if ( sieve_address_match(addrp, mctx, headers[i]) )
					matched = TRUE;				
			} 
		}
	}
	
	matched = sieve_match_end(mctx) || matched;

	t_pop();
	
	if ( result )
		sieve_interpreter_set_test_result(renv->interp, matched);
	
	return result;
}
