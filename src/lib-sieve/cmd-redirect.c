#include "lib.h"

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h" 

bool cmd_redirect_validate(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *arg;
		
	/* Check valid syntax 
	 *   Syntax:   redirect <address: string>
	 */
	if ( !sieve_validate_command_arguments(validator, cmd, 1, &arg) ||
	 	!sieve_validate_command_subtests(validator, cmd, 0) || 
	 	!sieve_validate_command_block(validator, cmd, FALSE, FALSE) ) {
	 	return FALSE;
	}

	/* Check argument */
	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "address", 1, SAAT_STRING) ) {
		return FALSE;
	}
	 
	return TRUE;
}
