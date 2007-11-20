#include "lib.h"

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h" 

/* Redirect command 
 * 
 * Syntax
 *   redirect <address: string>
 */

static bool cmd_redirect_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);

const struct sieve_command cmd_redirect = { 
	"redirect", 
	SCT_COMMAND,
	1, 0, FALSE, FALSE, 
	NULL, NULL,
	cmd_redirect_validate, 
	NULL, 
	NULL 
};

/* Validation */

static bool cmd_redirect_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *arg = cmd->first_positional;

	/* Check argument */
	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "address", 1, SAAT_STRING) ) {
		return FALSE;
	}
	 
	return TRUE;
}
