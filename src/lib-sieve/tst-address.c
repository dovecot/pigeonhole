#include <stdio.h>

#include "sieve-commands.h"
#include "sieve-commands-private.h"

#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-address-parts.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

/* Opcode */

static bool tst_address_opcode_dump
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);
static bool tst_address_opcode_execute
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);

const struct sieve_opcode tst_address_opcode = 
	{ tst_address_opcode_dump, tst_address_opcode_execute };

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

/* Optional arguments */

enum tst_address_optional {
	OPT_END,
	OPT_COMPARATOR,
	OPT_ADDRESS_PART,
	OPT_MATCH_TYPE
};

/* Test registration */

static bool tst_address_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(validator, cmd_reg, OPT_COMPARATOR );
	sieve_address_parts_link_tags(validator, cmd_reg, OPT_ADDRESS_PART);
	sieve_match_types_link_tags(validator, cmd_reg, OPT_MATCH_TYPE);

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

static bool tst_address_generate
	(struct sieve_generator *generator, struct sieve_command_context *ctx) 
{
	sieve_generator_emit_opcode(generator, SIEVE_OPCODE_ADDRESS);
	
	/* Generate arguments */  	
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;
	
	return TRUE;
}

/* Code dump */

static bool tst_address_opcode_dump
	(struct sieve_interpreter *interp, 
	struct sieve_binary *sbin, sieve_size_t *address)
{
	unsigned int opt_code;

	printf("ADDRESS\n");

	/* Handle any optional arguments */
	if ( sieve_operand_optional_present(sbin, address) ) {
		while ( (opt_code=sieve_operand_optional_read(sbin, address)) ) {
			switch ( opt_code ) {
			case OPT_COMPARATOR:
				if ( !sieve_opr_comparator_dump(interp, sbin, address) )
					return FALSE;
				break;
			case OPT_MATCH_TYPE:
				if ( !sieve_opr_match_type_dump(interp, sbin, address) )
					return FALSE;
				break;
			case OPT_ADDRESS_PART:
				if ( !sieve_opr_address_part_dump(interp, sbin, address) )
					return FALSE;
				break;			
			default:
				return FALSE;
			}
		}
	}

	return
		sieve_opr_stringlist_dump(sbin, address) &&
		sieve_opr_stringlist_dump(sbin, address);
}

/* Code execution */

static bool tst_address_opcode_execute
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address)
{
	struct mail *mail = sieve_interpreter_get_mail(interp);
	
	const struct sieve_comparator *cmp = &i_octet_comparator;
	const struct sieve_match_type *mtch = &is_match_type;
	const struct sieve_address_part *addrp = &all_address_part;
	unsigned int opt_code;
	struct sieve_match_context *mctx;
	struct sieve_coded_stringlist *hdr_list;
	struct sieve_coded_stringlist *key_list;
	string_t *hdr_item;
	bool matched;
	
	printf("?? ADDRESS\n");

	/* Handle any optional arguments */
	if ( sieve_operand_optional_present(sbin, address) ) {
		while ( (opt_code=sieve_operand_optional_read(sbin, address)) ) {
			switch ( opt_code ) {
			case OPT_COMPARATOR:
				if ( (cmp = sieve_opr_comparator_read(interp, sbin, address)) == NULL )
					return FALSE;
				break;
			case OPT_MATCH_TYPE:
				if ( (mtch = sieve_opr_match_type_read(interp, sbin, address)) == NULL )
					return FALSE;
				break;
			case OPT_ADDRESS_PART:
				if ( (addrp = sieve_opr_address_part_read(interp, sbin, address)) == NULL )
					return FALSE;
				break;
			default:
				return FALSE;
			}
		}
	}

	t_push();
		
	/* Read header-list */
	if ( (hdr_list=sieve_opr_stringlist_read(sbin, address)) == NULL ) {
		t_pop();
		return FALSE;
	}
	
	/* Read key-list */
	if ( (key_list=sieve_opr_stringlist_read(sbin, address)) == NULL ) {
		t_pop();
		return FALSE;
	}

	/* Initialize match context */
    mctx = sieve_match_begin(mtch, cmp, key_list);
	
	/* Iterate through all requested headers to match */
	hdr_item = NULL;
	matched = FALSE;
	while ( !matched && sieve_coded_stringlist_next_item(hdr_list, &hdr_item) && hdr_item != NULL ) {
		const char *const *headers;
			
		if ( mail_get_headers_utf8(mail, str_c(hdr_item), &headers) >= 0 ) {	
			
			int i;
			for ( i = 0; !matched && headers[i] != NULL; i++ ) {
				if ( sieve_address_match(addrp, mctx, headers[i]) )
					matched = TRUE;				
			} 
		}
	}
	
    sieve_match_end(mctx);

	t_pop();
	
	sieve_interpreter_set_test_result(interp, matched);
	
	return TRUE;
}
