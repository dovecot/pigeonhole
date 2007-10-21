#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h"

/* Test registration */

bool tst_address_registered(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	/* The order of these is not significant */
	sieve_validator_link_comparator_tag(validator, cmd_reg);
	sieve_validator_link_address_part_tags(validator, cmd_reg);
	sieve_validator_link_match_type_tags(validator, cmd_reg);

	return TRUE;
}

/* Test validation */

bool tst_address_validate(struct sieve_validator *validator, struct sieve_command_context *tst) 
{
	struct sieve_ast_argument *arg;
	
	/* Check envelope test syntax (optional tags are registered above):
	 *    address [ADDRESS-PART] [COMPARATOR] [MATCH-TYPE]
 	 *       <header-list: string-list> <key-list: string-list>
	 */
	if ( !sieve_validate_command_arguments(validator, tst, 2, &arg) ||
		!sieve_validate_command_subtests(validator, tst, 0) ) 
		return FALSE;
		
	tst->data = arg;
		
	if ( sieve_ast_argument_type(arg) != SAAT_STRING && sieve_ast_argument_type(arg) != SAAT_STRING_LIST ) {
		sieve_command_validate_error(validator, tst, 
			"the address test expects a string-list as first argument (header list), but %s was found", 
			sieve_ast_argument_name(arg));
		return FALSE; 
	}
	
	arg = sieve_ast_argument_next(arg);
	if ( sieve_ast_argument_type(arg) != SAAT_STRING && sieve_ast_argument_type(arg) != SAAT_STRING_LIST ) {
		sieve_command_validate_error(validator, tst, 
			"the address test expects a string-list as second argument (key list), but %s was found", 
			sieve_ast_argument_name(arg));
		return FALSE; 
	}
	
	return TRUE;
}

/* Test generation */

bool tst_address_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx __attr_unused__) 
{
	struct sieve_ast_argument *arg = (struct sieve_ast_argument *) ctx->data;
	sieve_generator_emit_core_opcode(generator, NULL, SIEVE_OPCODE_ADDRESS);
	
	/* Emit header names */  	
	if ( !sieve_generator_emit_stringlist_argument(generator, arg) ) 
		return FALSE;
	
	/* Emit key list */
	arg = sieve_ast_argument_next(arg);
	if ( !sieve_generator_emit_stringlist_argument(generator, arg) ) 
		return FALSE;
	
	return TRUE;
}

