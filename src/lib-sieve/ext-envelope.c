#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"

/* Forward declarations */

static bool ext_envelope_validator_load(struct sieve_validator *validator);
static bool tst_envelope_registered
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg);
static bool tst_envelope_validate
	(struct sieve_validator *validator, struct sieve_command_context *tst);

/* Extension definitions */

const struct sieve_extension envelope_extension = 
	{ "envelope", ext_envelope_validator_load, NULL, NULL, NULL };
static const struct sieve_command envelope_test = 
	{ "envelope", SCT_TEST, tst_envelope_registered, tst_envelope_validate, NULL, NULL };

/* Command Registration */
static bool tst_envelope_registered(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	/* The order of these is not significant */
	sieve_validator_link_comparator_tag(validator, cmd_reg);
	sieve_validator_link_address_part_tags(validator, cmd_reg);
	sieve_validator_link_match_type_tags(validator, cmd_reg);
	
	return TRUE;
}

/* Validation */
static bool tst_envelope_validate(struct sieve_validator *validator, struct sieve_command_context *tst) 
{ 		
	struct sieve_ast_argument *arg;
	
	/* Check envelope test syntax (optional tags are registered above):
	 *   envelope [COMPARATOR] [ADDRESS-PART] [MATCH-TYPE]
	 *     <envelope-part: string-list> <key-list: string-list>   
	 */
	if ( !sieve_validate_command_arguments(validator, tst, 2, &arg) ||
		!sieve_validate_command_subtests(validator, tst, 0) ) 
		return FALSE;
		
	if ( sieve_ast_argument_type(arg) != SAAT_STRING && sieve_ast_argument_type(arg) != SAAT_STRING_LIST ) {
		sieve_command_validate_error(validator, tst, 
			"the envelope test expects a string-list as first argument (envelope part), but %s was found", 
			sieve_ast_argument_name(arg));
		return FALSE; 
	}
	
	arg = sieve_ast_argument_next(arg);
	if ( sieve_ast_argument_type(arg) != SAAT_STRING && sieve_ast_argument_type(arg) != SAAT_STRING_LIST ) {
		sieve_command_validate_error(validator, tst, 
			"the envelope test expects a string-list as second argument (key list), but %s was found", 
			sieve_ast_argument_name(arg));
		return FALSE; 
	}
	
	return TRUE;
}

/* Load extension into validator */
static bool ext_envelope_validator_load(struct sieve_validator *validator)
{
	/* Register new test */
	sieve_validator_register_command(validator, &envelope_test);

	return TRUE;
}
