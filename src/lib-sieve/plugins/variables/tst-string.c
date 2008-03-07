#include <stdio.h>

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-code.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code-dumper.h"

#include "ext-variables-common.h"

/* String test 
 *
 * Syntax:
 *   string [COMPARATOR] [MATCH-TYPE]
 *     <source: string-list> <key-list: string-list>
 */

static bool tst_string_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool tst_string_validate
	(struct sieve_validator *validator, struct sieve_command_context *tst);
static bool tst_string_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx);

const struct sieve_command tst_string = { 
	"string", 
	SCT_TEST, 
	2, 0, FALSE, FALSE,
	tst_string_registered, 
	NULL,
	tst_string_validate, 
	tst_string_generate, 
	NULL 
};

/* Operations */

static bool tst_string_operation_dump
	(const struct sieve_operation *op, 
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static bool tst_string_operation_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation tst_string_operation = { 
	"STRING",
	&variables_extension, 
	EXT_VARIABLES_OPERATION_STRING, 
	tst_string_operation_dump, 
	tst_string_operation_execute 
};

/* Optional arguments */

enum tst_string_optional {	
	OPT_END,
	OPT_COMPARATOR,
	OPT_MATCH_TYPE
};

/* Test registration */

static bool tst_string_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(validator, cmd_reg, OPT_COMPARATOR);
	sieve_match_types_link_tags(validator, cmd_reg, OPT_MATCH_TYPE);

	return TRUE;
}

/* Test validation */

static bool tst_string_validate
	(struct sieve_validator *validator, struct sieve_command_context *tst) 
{ 		
	struct sieve_ast_argument *arg = tst->first_positional;
	
	if ( !sieve_validate_positional_argument
		(validator, tst, arg, "source", 1, SAAT_STRING_LIST) ) {
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

static bool tst_string_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx) 
{
	sieve_generator_emit_operation_ext
		(generator, &tst_string_operation, ext_variables_my_id);

 	/* Generate arguments */
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;
	
	return TRUE;
}

/* Code dump */

static bool tst_string_operation_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	int opt_code = 1;

	sieve_code_dumpf(denv, "STRING-TEST");
	sieve_code_descend(denv);

	/* Handle any optional arguments */
	if ( sieve_operand_optional_present(denv->sbin, address) ) {
		while ( opt_code != 0 ) {
			if ( !sieve_operand_optional_read(denv->sbin, address, &opt_code) ) 
				return FALSE;

			switch ( opt_code ) {
			case 0:
				break;
			case OPT_COMPARATOR:
				sieve_opr_comparator_dump(denv, address);
				break;
			case OPT_MATCH_TYPE:
				sieve_opr_match_type_dump(denv, address);
				break;
			default: 
				return FALSE;
			}
 		}
	}

	return
		sieve_opr_stringlist_dump(denv, address) &&
		sieve_opr_stringlist_dump(denv, address);
}

/* Code execution */

static bool tst_string_operation_execute
(const struct sieve_operation *op ATTR_UNUSED, 
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	bool result = TRUE;
	int opt_code = 1;
	const struct sieve_comparator *cmp = &i_octet_comparator;
	const struct sieve_match_type *mtch = &is_match_type;
	struct sieve_match_context *mctx;
	struct sieve_coded_stringlist *source;
	struct sieve_coded_stringlist *key_list;
	string_t *src_item;
	bool matched;
	
	printf("?? STRING\n");

	/* Handle any optional arguments */
	if ( sieve_operand_optional_present(renv->sbin, address) ) {
		while ( opt_code != 0 ) {
			if ( !sieve_operand_optional_read(renv->sbin, address, &opt_code) )
				return FALSE;

			switch ( opt_code ) {
			case 0: 
				break;
			case OPT_COMPARATOR:
				cmp = sieve_opr_comparator_read(renv, address);
				break;
			case OPT_MATCH_TYPE:
				mtch = sieve_opr_match_type_read(renv, address);
				break;
			default:
				return FALSE;
			}
		}
	}

	t_push();
		
	/* Read string-list */
	if ( (source=sieve_opr_stringlist_read(renv, address)) == NULL ) {
		t_pop();
		return FALSE;
	}
	
	/* Read key-list */
	if ( (key_list=sieve_opr_stringlist_read(renv, address)) == NULL ) {
		t_pop();
		return FALSE;
	}

	mctx = sieve_match_begin(renv->interp, mtch, cmp, key_list); 	

	/* Iterate through all requested strings to match */
	src_item = NULL;
	matched = FALSE;
	while ( !matched && 
		(result=sieve_coded_stringlist_next_item(source, &src_item)) 
		&& src_item != NULL ) {
			
		if ( sieve_match_value(mctx, str_c(src_item), str_len(src_item)) )
			matched = TRUE;				
	}

	matched = sieve_match_end(mctx) || matched; 	
	
	t_pop();
	
	if ( result )
		sieve_interpreter_set_test_result(renv->interp, matched);
	
	return result;
}
