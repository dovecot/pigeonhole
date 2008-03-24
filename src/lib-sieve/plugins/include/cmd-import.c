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
static bool cmd_import_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED);
		
static bool opc_import_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);
		
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
	cmd_import_generate, 
	NULL
};

/* Import operation */

const struct sieve_operation import_operation = { 
	"import",
	&include_extension,
	EXT_INCLUDE_OPERATION_IMPORT,
	NULL, 
	opc_import_execute 
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

static bool cmd_import_generate
(struct sieve_generator *gentr, struct sieve_command_context *ctx ATTR_UNUSED) 
{
	sieve_generator_emit_operation_ext	
		(gentr, &import_operation, ext_include_my_id);

	return TRUE;
}

/*
 * Interpretation
 */

static bool opc_import_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, 
	sieve_size_t *address ATTR_UNUSED)
{	
	return TRUE;
}


