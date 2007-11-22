#include "lib.h"

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-imapflags-common.h"

bool ext_imapflags_command_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd)
{
	struct sieve_ast_argument *arg = cmd->first_positional;
	struct sieve_ast_argument *arg2;
	
	/* Check arguments */
	
	if ( arg == NULL ) {
		sieve_command_validate_error(validator, cmd, 
			"the %s command expects at least one argument, but none was found", 
			cmd->command->identifier);
		return FALSE;
	}
	
	if ( sieve_ast_argument_type(arg) != SAAT_STRING && 
		sieve_ast_argument_type(arg) != SAAT_STRING_LIST ) 
	{
		sieve_command_validate_error(validator, cmd, 
			"the %s command expects either a string (variable name) or "
			"a string-list (list of flags) as first argument, but %s was found", 
			cmd->command->identifier, sieve_ast_argument_name(arg));
		return FALSE; 
	}
	//sieve_validator_argument_activate(validator, arg);

	arg2 = sieve_ast_argument_next(arg);
	
	if ( arg2 != NULL ) {
		/* First, check syntax sanity */
				
		if ( sieve_ast_argument_type(arg) != SAAT_STRING ) 
		{
			sieve_command_validate_error(validator, cmd, 
				"if a second argument is specified for the %s command, the first "
				"must be a string (variable name), but %s was found",
				cmd->command->identifier, sieve_ast_argument_name(arg));
			return FALSE; 
		}		
		
		if ( sieve_ast_argument_type(arg2) != SAAT_STRING && 
			sieve_ast_argument_type(arg2) != SAAT_STRING_LIST ) 
		{
			sieve_command_validate_error(validator, cmd, 
				"the %s command expects a string list (list of flags) as "
				"second argument when two arguments are specified, "
				"but %s was found",
				cmd->command->identifier, sieve_ast_argument_name(arg2));
			return FALSE; 
		}
		
		/* Then, check whether the second argument is permitted */
		
		/* IF !VARIABLE EXTENSION LOADED */
		{
			sieve_command_validate_error(validator, cmd, 
				"the %s command only allows for the specification of a "
				"variable name when the variables extension is active",
				cmd->command->identifier);
			return FALSE;
		}

		//sieve_validator_argument_activate(validator, arg2);
	}	
	return TRUE;
}

