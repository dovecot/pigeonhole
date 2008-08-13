#include "lib.h"

#include "sieve-code.h"
#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "sieve-ext-variables.h"

#include "ext-include-common.h"
#include "ext-include-binary.h"
#include "ext-include-variables.h"

/* 
 * Commands 
 */

static bool cmd_import_validate
  (struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_import_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *cmd);
		
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
	cmd_import_validate, 
	cmd_import_generate, 
	NULL
};

/*
 * Operations
 */

static bool opc_import_dump
	(const struct sieve_operation *op,	
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int opc_import_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

/* Import operation */

const struct sieve_operation import_operation = { 
	"import",
	&include_extension,
	EXT_INCLUDE_OPERATION_IMPORT,
	opc_import_dump, 
	opc_import_execute
};

/* Export operation */

const struct sieve_operation export_operation = { 
	"export",
	&include_extension,
	EXT_INCLUDE_OPERATION_EXPORT,
	opc_import_dump, 
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
			prev_context->command != &cmd_import &&
			(cmd->command != &cmd_export || prev_context->command != &cmd_export) ) ) 
	{	
		sieve_command_validate_error(validator, cmd, 
			"%s commands can only be placed at top level "
			"at the beginning of the file after any require %scommands",
			cmd->command->identifier, cmd->command == &cmd_export ? "or import " : "");
		return FALSE;
	}
	
	if ( !sieve_ext_variables_is_active(validator) ) {
		sieve_command_validate_error(validator, cmd, 
			"%s command requires that variables extension is active",
			cmd->command->identifier);
		return FALSE;
	}
		
	/* Register imported variable */
	if ( sieve_ast_argument_type(arg) == SAAT_STRING ) {
		/* Single string */
		const char *identifier = sieve_ast_argument_strc(arg);
		struct sieve_variable *var;
		
		if ( (var=ext_include_variable_import_global
			(validator, cmd, identifier, cmd->command == &cmd_export)) == NULL )
			return FALSE;
			
		arg->context = (void *) var;

	} else if ( sieve_ast_argument_type(arg) == SAAT_STRING_LIST ) {
		/* String list */
		struct sieve_ast_argument *stritem = sieve_ast_strlist_first(arg);
		
		while ( stritem != NULL ) {
			const char *identifier = sieve_ast_argument_strc(stritem);
			struct sieve_variable *var;
			
			if ( (var=ext_include_variable_import_global
				(validator, cmd, identifier, cmd->command == &cmd_export)) == NULL )
				return FALSE;

			stritem->context = (void *) var;
	
			stritem = sieve_ast_strlist_next(stritem);
		}
	} else {
		/* Something else */
		sieve_command_validate_error(validator, cmd, 
			"the %s command accepts a single string or string list argument, "
			"but %s was found", cmd->command->identifier,
			sieve_ast_argument_name(arg));
		return FALSE;
	}
	
	/* Join emport and export commands with predecessors if possible */
	if ( prev_context->command == cmd->command ) {
		/* Join this command's string list with the previous one */
		prev_context->first_positional = sieve_ast_stringlist_join
			(prev_context->first_positional, cmd->first_positional);
		
		if ( prev_context->first_positional == NULL ) {
			sieve_command_validate_error(validator, cmd, 
				"compiler reached AST limit (script too complex)");
			return FALSE;
		}

		/* Detach this command node */
		sieve_ast_node_detach(cmd->ast_node);
	}
		
	return TRUE;
}

/*
 * Code generation
 */
 
static bool cmd_import_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *arg = cmd->first_positional;

	if ( cmd->command == &cmd_import )
		sieve_operation_emit_code(cgenv->sbin, &import_operation);
	else
		sieve_operation_emit_code(cgenv->sbin, &export_operation);
 	 			
	if ( sieve_ast_argument_type(arg) == SAAT_STRING ) {
		/* Single string */
		struct sieve_variable *var = (struct sieve_variable *) arg->context;
		
		(void)sieve_binary_emit_integer(cgenv->sbin, 1);
		(void)sieve_binary_emit_integer(cgenv->sbin, var->index);
		if ( cmd->command == &cmd_import )
			(void)sieve_code_source_line_emit(cgenv->sbin, arg->source_line);
		
	} else if ( sieve_ast_argument_type(arg) == SAAT_STRING_LIST ) {
		/* String list */
		struct sieve_ast_argument *stritem = sieve_ast_strlist_first(arg);
		
		(void)sieve_binary_emit_integer(cgenv->sbin, sieve_ast_strlist_count(arg));
						
		while ( stritem != NULL ) {
			struct sieve_variable *var = (struct sieve_variable *) stritem->context;
			
			(void)sieve_binary_emit_integer(cgenv->sbin, var->index);
			
			if ( cmd->command == &cmd_import )
				(void)sieve_code_source_line_emit(cgenv->sbin, stritem->source_line);

			stritem = sieve_ast_strlist_next(stritem);
		}
	} else {
		i_unreached();
	}
 	 		
	return TRUE;
}

/* 
 * Code dump
 */
 
static bool opc_import_dump
(const struct sieve_operation *op,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	unsigned int count, i;
	struct sieve_variable_scope *scope;
	struct sieve_variable * const *vars;
	unsigned var_count;
	
	if ( !sieve_binary_read_integer(denv->sbin, address, &count) )
		return FALSE;

	if ( op == &import_operation )
		sieve_code_dumpf(denv, "IMPORT (count: %u):", count);
	else
		sieve_code_dumpf(denv, "EXPORT (count: %u):", count);

	scope = ext_include_binary_get_global_scope(denv->sbin);
	vars = sieve_variable_scope_get_variables(scope, &var_count);

	sieve_code_descend(denv);

	for ( i = 0; i < count; i++ ) {
		unsigned int index;
		
		sieve_code_mark(denv);
		if ( !sieve_binary_read_integer(denv->sbin, address, &index) ||
			index >= var_count )
			return FALSE;
			
		sieve_code_dumpf(denv, "GLOBAL VAR[%d]: '%s'", 
			index, vars[index]->identifier); 
		
		if ( op == &import_operation ) {
			sieve_code_descend(denv);

			if ( !sieve_code_source_line_dump(denv, address) )
				return FALSE;
				
			sieve_code_ascend(denv);
		}
	}
	 
	return TRUE;
}

/* 
 * Execution
 */
 
static int opc_import_execute
(const struct sieve_operation *op,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	unsigned int count, i;
		
	if ( !sieve_binary_read_integer(renv->sbin, address, &count) ) {
		sieve_runtime_trace_error(renv, "invalid count operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	for ( i = 0; i < count; i++ ) {
		unsigned int index, source_line;
		
		if ( !sieve_binary_read_integer(renv->sbin, address, &index) ) {
			sieve_runtime_trace_error(renv, "invalid global variable operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}
		
		if ( op == &import_operation &&
			!sieve_code_source_line_read(renv, address, &source_line) ) {
			sieve_runtime_trace_error(renv, "invalid source line operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}
		
		/* FIXME: do something */
	}

	return SIEVE_EXEC_OK;
}


