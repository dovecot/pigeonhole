#include "lib.h"

#include "sieve-code.h"
#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-ext-variables.h"

#include "ext-include-common.h"
#include "ext-include-variables.h"

/* Forward declarations */

static bool cmd_export_validate
  (struct sieve_validator *validator, struct sieve_command_context *cmd);
		
/* Export command 
 * 
 * Syntax
 *   export
 */	
const struct sieve_command cmd_export = { 
	"export", 
	SCT_COMMAND, 
	1, 0, FALSE, FALSE,
	NULL, NULL, 
	cmd_export_validate, 
	NULL, NULL
};

/*
 * Validation
 */

static bool cmd_export_validate
  (struct sieve_validator *validator, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *arg = cmd->first_positional;
	struct sieve_command_context *prev_context = 
		sieve_command_prev_context(cmd);
		
	/* Check valid command placement */
	if ( !sieve_command_is_toplevel(cmd) ||
		( !sieve_command_is_first(cmd) && prev_context != NULL &&
			prev_context->command != &cmd_require && 
			prev_context->command != &cmd_import &&
			prev_context->command != &cmd_export) ) 
	{	
		sieve_command_validate_error(validator, cmd, 
			"export commands can only be placed at top level "
			"at the beginning of the file after any require or import commands");
		return FALSE;
	}
	
	if ( !sieve_ext_variables_is_active(validator) ) {
		sieve_command_validate_error(validator, cmd, 
			"export command requires that variables extension is active");
		return FALSE;
	}
	
	/* Register exported variable */
	if ( sieve_ast_argument_type(arg) == SAAT_STRING ) {
		/* Single string */
		const char *variable = sieve_ast_argument_strc(arg);
		
		if ( !ext_include_variable_export(arg->ast, variable) ) {
			sieve_command_validate_error(validator, cmd, 
				"cannot export imported variable '%s'", variable);
			return FALSE;
		}

	} else if ( sieve_ast_argument_type(arg) == SAAT_STRING_LIST ) {
		/* String list */
		struct sieve_ast_argument *stritem = sieve_ast_strlist_first(arg);
		
		while ( stritem != NULL ) {
			const char *variable = sieve_ast_argument_strc(stritem);
			
			if ( !ext_include_variable_export(arg->ast, variable) ) {
				sieve_command_validate_error(validator, cmd, 
					"cannot export imported variable '%s'", variable);
				return FALSE;
			}
	
			stritem = sieve_ast_strlist_next(stritem);
		}
	} else {
		/* Something else */
		sieve_command_validate_error(validator, cmd, 
			"the export command accepts a single string or string list argument, "
			"but %s was found", 
			sieve_ast_argument_name(arg));
		return FALSE;
	}
	
	(void)sieve_ast_arguments_detach(arg, 1);
	return TRUE;
}
