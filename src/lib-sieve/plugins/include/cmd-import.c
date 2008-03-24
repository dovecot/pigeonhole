#include "lib.h"

#include "sieve-code.h"
#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-ext-variables.h"

#include "ext-include-common.h"

/* Forward declarations */

static bool cmd_import_validate
  (struct sieve_validator *validator, struct sieve_command_context *cmd); 
		
/* Import command 
 * 
 * Syntax
 *   import
 */	
const struct sieve_command cmd_import = { 
	"import", 
	SCT_COMMAND, 
	1, 0, FALSE, FALSE,
	NULL, NULL,
	cmd_import_validate, 
	NULL, NULL
};

/*
 * Validation
 */

static bool cmd_import_validate
  (struct sieve_validator *validator, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *arg = cmd->first_positional;
	struct sieve_command_context *prev_context = 
		sieve_command_prev_context(cmd);
		
	/* Check valid command placement */
	if ( !sieve_command_is_toplevel(cmd) ||
		( !sieve_command_is_first(cmd) && prev_context != NULL &&
			prev_context->command != &cmd_require && 
			prev_context->command != &cmd_import) ) 
	{	
		sieve_command_validate_error(validator, cmd, 
			"import commands can only be placed at top level "
			"at the beginning of the file after any require commands");
		return FALSE;
	}
	
	if ( !sieve_ext_variables_is_active(validator) ) {
		sieve_command_validate_error(validator, cmd, 
			"import command requires that variables extension is active");
		return FALSE;
	}
		
	/* Register imported variable */
	if ( sieve_ast_argument_type(arg) == SAAT_STRING ) {
		/* Single string */
		const char *variable = sieve_ast_argument_strc(arg);
		
		ext_include_import_variable(arg->ast, variable);

	} else if ( sieve_ast_argument_type(arg) == SAAT_STRING_LIST ) {
		/* String list */
		struct sieve_ast_argument *stritem = sieve_ast_strlist_first(arg);
		
		while ( stritem != NULL ) {
			const char *variable = sieve_ast_argument_strc(stritem);
			ext_include_import_variable(arg->ast, variable);
	
			stritem = sieve_ast_strlist_next(stritem);
		}
	} else {
		/* Something else */
		sieve_command_validate_error(validator, cmd, 
			"the import command accepts a single string or string list argument, "
			"but %s was found", 
			sieve_ast_argument_name(arg));
		return FALSE;
	}
	
	(void)sieve_ast_arguments_detach(arg, 1);
	return TRUE;
}

