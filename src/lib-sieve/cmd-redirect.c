#include "lib.h"

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h" 

bool cmd_redirect_validate(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *argument;
		
	/* Check valid syntax 
	 *   Syntax:   redirect <address: string>
	 */
	if ( !sieve_validate_command_arguments(validator, cmd, 1, &argument) ||
	 	!sieve_validate_command_subtests(validator, cmd, 0) || 
	 	!sieve_validate_command_block(validator, cmd, FALSE, FALSE) ) {
	 	return FALSE;
	}

	/* Check argument */
	if ( sieve_ast_argument_type(argument) != SAAT_STRING ) {
		/* Somethin else */
		sieve_command_validate_error(validator, cmd, 
			"the redirect command accepts a single string argument (address) but %s was found", 
			sieve_ast_argument_name(argument));
		return FALSE;
	}
	 
	return TRUE;
}
