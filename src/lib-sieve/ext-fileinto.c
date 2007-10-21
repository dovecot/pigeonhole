#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"

/* Forward declarations */
static bool ext_fileinto_validator_load(struct sieve_validator *validator);
static bool cmd_fileinto_validate(struct sieve_validator *validator, struct sieve_command_context *cmd);

/* Extension definitions */
const struct sieve_extension fileinto_extension = 
	{ "fileinto", ext_fileinto_validator_load, NULL, NULL, NULL };
static const struct sieve_command fileinto_command = 
	{ "fileinto", SCT_COMMAND, NULL, cmd_fileinto_validate, NULL, NULL };

/* Validation */
static bool cmd_fileinto_validate(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{ 	
	/* Check valid syntax: 
	 *    reject <reason: string>
	 */
	if ( !sieve_validate_command_arguments(validator, cmd, 1, NULL) ||
		!sieve_validate_command_subtests(validator, cmd, 0) || 
	 	!sieve_validate_command_block(validator, cmd, FALSE, FALSE) ) {
	 	
		return FALSE;
	}
	
	return TRUE;
}

/* Load extension into validator */
static bool ext_fileinto_validator_load(struct sieve_validator *validator)
{
	/* Register new command */
	sieve_validator_register_command(validator, &fileinto_command);

	return TRUE;
}
