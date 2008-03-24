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

static bool cmd_export_validate
  (struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_export_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED);

static bool opc_export_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);
		
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
	cmd_export_generate, 
	NULL
};

/* Export operation */

const struct sieve_operation export_operation = { 
	"export",
	&include_extension,
	EXT_INCLUDE_OPERATION_EXPORT,
	NULL, 
	opc_export_execute 
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
	
	if ( !sieve_validate_positional_argument
		(validator, cmd, arg, "value", 1, SAAT_STRING_LIST) ) {
		return FALSE;
	}
	
	//return sieve_validator_argument_activate(validator, cmd, arg, FALSE);
	return TRUE;
}

/*
 * Generation
 */

static bool cmd_export_generate
(struct sieve_generator *gentr, struct sieve_command_context *ctx ATTR_UNUSED) 
{
	sieve_generator_emit_operation_ext	
		(gentr, &export_operation, ext_include_my_id);

	return TRUE;
}

/*
 * Interpretation
 */

static bool opc_export_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, 
	sieve_size_t *address ATTR_UNUSED)
{	
	return TRUE;
}


