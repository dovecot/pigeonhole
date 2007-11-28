#include <stdio.h>

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

/* Header test 
 *
 * Syntax:
 *   header [COMPARATOR] [MATCH-TYPE]
 *     <header-names: string-list> <key-list: string-list>
 */

static bool tst_header_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool tst_header_validate
	(struct sieve_validator *validator, struct sieve_command_context *tst);
static bool tst_header_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx);

const struct sieve_command tst_header = { 
	"header", 
	SCT_TEST, 
	2, 0, FALSE, FALSE,
	tst_header_registered, 
	NULL,
	tst_header_validate, 
	tst_header_generate, 
	NULL 
};

/* Opcode */

static bool tst_header_opcode_dump
	(const struct sieve_opcode *opcode, 
		const struct sieve_runtime_env *runenv, sieve_size_t *address);
static bool tst_header_opcode_execute
	(const struct sieve_opcode *opcode, 
		const struct sieve_runtime_env *runenv, sieve_size_t *address);

const struct sieve_opcode tst_header_opcode = { 
	"HEADER",
	SIEVE_OPCODE_HEADER,
	NULL, 0, 
	tst_header_opcode_dump, 
	tst_header_opcode_execute 
};

/* Optional arguments */

enum tst_header_optional {	
	OPT_END,
	OPT_COMPARATOR,
	OPT_MATCH_TYPE
};

/* Test registration */

static bool tst_header_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(validator, cmd_reg, OPT_COMPARATOR);
	sieve_match_types_link_tags(validator, cmd_reg, OPT_MATCH_TYPE);

	return TRUE;
}

/* Test validation */

static bool tst_header_validate
	(struct sieve_validator *validator, struct sieve_command_context *tst) 
{ 		
	struct sieve_ast_argument *arg = tst->first_positional;
	
	if ( !sieve_validate_positional_argument
		(validator, tst, arg, "header names", 1, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	sieve_validator_argument_activate(validator, arg);
	
	arg = sieve_ast_argument_next(arg);

	if ( !sieve_validate_positional_argument
		(validator, tst, arg, "key list", 2, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	sieve_validator_argument_activate(validator, arg);

	/* Validate the key argument to a specified match type */
  sieve_match_type_validate(validator, tst, arg);
	
	return TRUE;
}

/* Test generation */

static bool tst_header_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx) 
{
	sieve_generator_emit_opcode(generator, &tst_header_opcode);

 	/* Generate arguments */
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;
	
	return TRUE;
}

/* Code dump */

static bool tst_header_opcode_dump
(const struct sieve_opcode *opcode ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	int opt_code = 1;

	printf("HEADER\n");

	/* Handle any optional arguments */
	if ( sieve_operand_optional_present(renv->sbin, address) ) {
		while ( opt_code != 0 ) {
			if ( !sieve_operand_optional_read(renv->sbin, address, &opt_code) ) 
				return FALSE;

			switch ( opt_code ) {
			case 0:
				break;
			case OPT_COMPARATOR:
				sieve_opr_comparator_dump(renv->sbin, address);
				break;
			case OPT_MATCH_TYPE:
				sieve_opr_match_type_dump(renv->sbin, address);
				break;
			default: 
				return FALSE;
			}
 		}
	}

	return
		sieve_opr_stringlist_dump(renv->sbin, address) &&
		sieve_opr_stringlist_dump(renv->sbin, address);
}

/* Code execution */

static bool tst_header_opcode_execute
(const struct sieve_opcode *opcode ATTR_UNUSED, 
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	int opt_code = 1;
	const struct sieve_comparator *cmp = &i_octet_comparator;
	const struct sieve_match_type *mtch = &is_match_type;
	struct sieve_match_context *mctx;
	struct sieve_coded_stringlist *hdr_list;
	struct sieve_coded_stringlist *key_list;
	string_t *hdr_item;
	bool matched;
	
	printf("?? HEADER\n");

	/* Handle any optional arguments */
	if ( sieve_operand_optional_present(renv->sbin, address) ) {
		while ( opt_code != 0 ) {
			if ( !sieve_operand_optional_read(renv->sbin, address, &opt_code) )
				return FALSE;

			switch ( opt_code ) {
			case 0: 
				break;
			case OPT_COMPARATOR:
				cmp = sieve_opr_comparator_read(renv->sbin, address);
				break;
			case OPT_MATCH_TYPE:
				mtch = sieve_opr_match_type_read(renv->sbin, address);
				break;
			default:
				return FALSE;
			}
		}
	}

	t_push();
		
	/* Read header-list */
	if ( (hdr_list=sieve_opr_stringlist_read(renv->sbin, address)) == NULL ) {
		t_pop();
		return FALSE;
	}
	
	/* Read key-list */
	if ( (key_list=sieve_opr_stringlist_read(renv->sbin, address)) == NULL ) {
		t_pop();
		return FALSE;
	}

	mctx = sieve_match_begin(mtch, cmp, key_list); 	

	/* Iterate through all requested headers to match */
	hdr_item = NULL;
	matched = FALSE;
	while ( !matched && sieve_coded_stringlist_next_item(hdr_list, &hdr_item) && hdr_item != NULL ) {
		const char *const *headers;
			
		if ( mail_get_headers_utf8(renv->msgdata->mail, str_c(hdr_item), &headers) >= 0 ) {	
			
			int i;
			for ( i = 0; !matched && headers[i] != NULL; i++ ) {
				if ( sieve_match_value(mctx, headers[i]) )
					matched = TRUE;				
			} 
		}
	}

	matched = sieve_match_end(mctx) || matched; 	
	
	t_pop();
	
	sieve_interpreter_set_test_result(renv->interp, matched);
	
	return TRUE;
}
