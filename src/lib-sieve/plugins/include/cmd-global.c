/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-common.h"
#include "sieve-code.h"
#include "sieve-commands.h"
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

static bool cmd_global_validate
  (struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_global_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *cmd);

const struct sieve_command cmd_global = {
    "global",
    SCT_COMMAND,
    1, 0, FALSE, FALSE,
    NULL, NULL,
    cmd_global_validate,
    cmd_global_generate,
    NULL
};

/* DEPRICATED:
 */
		
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
	cmd_global_validate, 
	cmd_global_generate, 
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
	cmd_global_validate, 
	cmd_global_generate, 
	NULL
};

/*
 * Operations
 */

static bool opc_global_dump
	(const struct sieve_operation *op,	
		const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int opc_global_execute
	(const struct sieve_operation *op, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

/* Global operation */

const struct sieve_operation global_operation = { 
	"global",
	&include_extension,
	EXT_INCLUDE_OPERATION_GLOBAL,
	opc_global_dump, 
	opc_global_execute
};

/*
 * Validation
 */

static bool cmd_global_validate
  (struct sieve_validator *validator, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *arg = cmd->first_positional;
	struct sieve_command_context *prev_context = 
		sieve_command_prev_context(cmd);

	/* Check valid command placement */
	if ( !sieve_command_is_toplevel(cmd) ||
		( !sieve_command_is_first(cmd) && prev_context != NULL &&
			prev_context->command != &cmd_require ) ) {

		if ( cmd->command == &cmd_global ) {
			if ( prev_context->command != &cmd_global ) {
				sieve_command_validate_error(validator, cmd, 
					"a global command can only be placed at top level "
					"at the beginning of the file after any require or other global commands");
				return FALSE;
			}
		} else {
			if ( prev_context->command != &cmd_import && prev_context->command != &cmd_export ) {
                sieve_command_validate_error(validator, cmd,
                    "the DEPRICATED %s command can only be placed at top level "
                    "at the beginning of the file after any require or import/export commands",
					cmd->command->identifier);
                return FALSE;
            }
		}
	}

	/* Check for use of variables extension */	
	if ( !sieve_ext_variables_is_active(validator) ) {
		sieve_command_validate_error(validator, cmd, 
			"%s command requires that variables extension is active",
			cmd->command->identifier);
		return FALSE;
	}
		
	/* Register global variable */
	if ( sieve_ast_argument_type(arg) == SAAT_STRING ) {
		/* Single string */
		const char *identifier = sieve_ast_argument_strc(arg);
		struct sieve_variable *var;
		
		if ( (var=ext_include_variable_import_global
			(validator, cmd, identifier)) == NULL )
			return FALSE;
			
		arg->context = (void *) var;

	} else if ( sieve_ast_argument_type(arg) == SAAT_STRING_LIST ) {
		/* String list */
		struct sieve_ast_argument *stritem = sieve_ast_strlist_first(arg);
		
		while ( stritem != NULL ) {
			const char *identifier = sieve_ast_argument_strc(stritem);
			struct sieve_variable *var;
			
			if ( (var=ext_include_variable_import_global
				(validator, cmd, identifier)) == NULL )
				return FALSE;

			stritem->context = (void *) var;
	
			stritem = sieve_ast_strlist_next(stritem);
		}
	} else {
		/* Something else */
		sieve_argument_validate_error(validator, arg, 
			"the %s command accepts a single string or string list argument, "
			"but %s was found", cmd->command->identifier,
			sieve_ast_argument_name(arg));
		return FALSE;
	}
	
	/* Join global commands with predecessors if possible */
	if ( prev_context->command == cmd->command ) {
		/* Join this command's string list with the previous one */
		prev_context->first_positional = sieve_ast_stringlist_join
			(prev_context->first_positional, cmd->first_positional);
		
		if ( prev_context->first_positional == NULL ) {
			/* Not going to happen unless MAXINT stringlist items are specified */
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
 
static bool cmd_global_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *arg = cmd->first_positional;

	sieve_operation_emit_code(cgenv->sbin, &global_operation);
 	 			
	if ( sieve_ast_argument_type(arg) == SAAT_STRING ) {
		/* Single string */
		struct sieve_variable *var = (struct sieve_variable *) arg->context;
		
		(void)sieve_binary_emit_unsigned(cgenv->sbin, 1);
		(void)sieve_binary_emit_unsigned(cgenv->sbin, var->index);
		
	} else if ( sieve_ast_argument_type(arg) == SAAT_STRING_LIST ) {
		/* String list */
		struct sieve_ast_argument *stritem = sieve_ast_strlist_first(arg);
		
		(void)sieve_binary_emit_unsigned(cgenv->sbin, sieve_ast_strlist_count(arg));
						
		while ( stritem != NULL ) {
			struct sieve_variable *var = (struct sieve_variable *) stritem->context;
			
			(void)sieve_binary_emit_unsigned(cgenv->sbin, var->index);
			
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
 
static bool opc_global_dump
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	unsigned int count, i, var_count;
	struct sieve_variable_scope *scope;
	struct sieve_variable * const *vars;
	
	if ( !sieve_binary_read_unsigned(denv->sbin, address, &count) )
		return FALSE;

	sieve_code_dumpf(denv, "GLOBAL (count: %u):", count);

	scope = ext_include_binary_get_global_scope(denv->sbin);
	vars = sieve_variable_scope_get_variables(scope, &var_count);

	sieve_code_descend(denv);

	for ( i = 0; i < count; i++ ) {
		unsigned int index;
		
		sieve_code_mark(denv);
		if ( !sieve_binary_read_unsigned(denv->sbin, address, &index) ||
			index >= var_count )
			return FALSE;
			
		sieve_code_dumpf(denv, "VAR[%d]: '%s'", index, vars[index]->identifier); 
	}
	 
	return TRUE;
}

/* 
 * Execution
 */
 
static int opc_global_execute
(const struct sieve_operation *op ATTR_UNUSED,
	const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	struct sieve_variable_scope *scope;	
	struct sieve_variable_storage *storage;
	struct sieve_variable * const *vars;
	unsigned int var_count, count, i;
		
	if ( !sieve_binary_read_unsigned(renv->sbin, address, &count) ) {
		sieve_runtime_trace_error(renv, "invalid count operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
	
	scope = ext_include_binary_get_global_scope(renv->sbin);
	vars = sieve_variable_scope_get_variables(scope, &var_count);
	storage = ext_include_interpreter_get_global_variables(renv->interp);

	for ( i = 0; i < count; i++ ) {
		unsigned int index;
		
		if ( !sieve_binary_read_unsigned(renv->sbin, address, &index) ) {
			sieve_runtime_trace_error(renv, "invalid global variable operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}
		
		if ( index >= var_count ) {
			sieve_runtime_trace_error(renv, "invalid global variable index (%u > %u)",
				index, var_count);
			return SIEVE_EXEC_BIN_CORRUPT;
		}
		
		/* Make sure variable is initialized (export) */
		(void)sieve_variable_get_modifiable(storage, index, NULL); 
	}

	return SIEVE_EXEC_OK;
}


