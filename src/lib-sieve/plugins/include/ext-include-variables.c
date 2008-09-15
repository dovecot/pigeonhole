/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-script.h"
#include "sieve-ast.h"
#include "sieve-binary.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "sieve-ext-variables.h"

#include "ext-include-common.h"
#include "ext-include-binary.h"
#include "ext-include-variables.h"

/*
 * Types
 */

enum ext_include_variable_type {
	EXT_INCLUDE_VAR_IMPORTED,
	EXT_INCLUDE_VAR_EXPORTED,
	EXT_INCLUDE_VAR_INVALID
};

struct ext_include_variable {
	enum ext_include_variable_type type;
	unsigned int source_line;
};

/* 
 * Variable import-export
 */
 
struct sieve_variable *ext_include_variable_import_global
(struct sieve_validator *valdtr, struct sieve_command_context *cmd, 
	const char *variable, bool export)
{
	struct sieve_ast *ast = cmd->ast_node->ast;
	struct ext_include_ast_context *ctx = ext_include_get_ast_context(ast);
	struct sieve_variable_scope *main_scope;
	struct sieve_variable *var = NULL, *impvar = NULL;

	/* Check if the requested variable was imported already */
	if ( export && 
		(impvar=sieve_variable_scope_get_variable(ctx->import_vars, variable, FALSE))
		!= NULL ) {
		if ( export ) {
			/* Yes, and now export is attempted. ERROR */
			sieve_command_validate_error(valdtr, cmd, 
				"cannot export imported variable '%s'", variable);
			return NULL;
		} else {
			/* Yes, and it is imported again. Warn the user */
			if ( impvar->context != NULL ) {
				struct ext_include_variable *varctx = 
					(struct ext_include_variable *) impvar->context;
				sieve_command_validate_warning(valdtr, cmd,
	                "variable '%s' already imported earlier at line %d", variable, 
					varctx->source_line);
			}
		}
	}

	/* Sanity safeguard */	
	i_assert ( ctx->global_vars != NULL );
		
	/* Get/Declare the variable in the global scope */
	var = sieve_variable_scope_get_variable(ctx->global_vars, variable, TRUE);

	/* Check whether scope is over its size limit */
	if ( var == NULL ) {
		sieve_command_validate_error(valdtr, cmd,
			"declaration of new global variable '%s' exceeds the limit "
			"(max variables: %u)", 
			variable, SIEVE_VARIABLES_MAX_SCOPE_SIZE);
	}

	/* Assign context for creation of symbol block during code generation */
	if ( var->context == NULL ) {
		pool_t pool = sieve_variable_scope_pool(ctx->global_vars);
		struct ext_include_variable *varctx;

		/* We only record data from the first encounter */
		varctx = p_new(pool, struct ext_include_variable, 1);
		varctx->type = export ? 
			EXT_INCLUDE_VAR_EXPORTED : EXT_INCLUDE_VAR_IMPORTED;
		varctx->source_line = cmd->ast_node->source_line;
		var->context = varctx;
	}
	
	/* Import the global variable into the local script scope */	
	main_scope = sieve_ext_variables_get_main_scope(valdtr);
	(void)sieve_variable_scope_import(main_scope, var);

	/* If this is an import it needs to be registered to detect duplicates */
	if ( !export && impvar == NULL ) { 
		pool_t pool = sieve_variable_scope_pool(ctx->import_vars);
		struct ext_include_variable *varctx;

		impvar = sieve_variable_scope_declare(ctx->import_vars, variable);

		i_assert( impvar != NULL );

		varctx = p_new(pool, struct ext_include_variable, 1);
		varctx->type = EXT_INCLUDE_VAR_IMPORTED;
		impvar->context = varctx;
	}

	return var;	
}

/*
 * Binary symbol table
 */
 
bool ext_include_variables_save
(struct sieve_binary *sbin, struct sieve_variable_scope *global_vars)
{
	unsigned int count = sieve_variable_scope_size(global_vars);

	sieve_binary_emit_unsigned(sbin, count);

	if ( count > 0 ) {
		unsigned int size, i;
		struct sieve_variable *const *vars = 
			sieve_variable_scope_get_variables(global_vars, &size);

		for ( i = 0; i < size; i++ ) {
			struct ext_include_variable *varctx =
				(struct ext_include_variable *) vars[i]->context;

			i_assert( varctx != NULL );
			
			sieve_binary_emit_byte(sbin, varctx->type);
			sieve_binary_emit_cstring(sbin, vars[i]->identifier);
		}
	}

	return TRUE;
}

bool ext_include_variables_load
(struct sieve_binary *sbin, sieve_size_t *offset, unsigned int block,
	struct sieve_variable_scope **global_vars_r)
{
	unsigned int count = 0;
	unsigned int i;
	pool_t pool;

	/* Sanity assert */
	i_assert( *global_vars_r == NULL );

	if ( !sieve_binary_read_unsigned(sbin, offset, &count) ) {
		sieve_sys_error("include: failed to read global variables count "
			"from dependency block %d of binary %s", block, sieve_binary_path(sbin));
		return FALSE;
	}

	if ( count > SIEVE_VARIABLES_MAX_SCOPE_SIZE ) {
		sieve_sys_error("include: global variable scope size of binary %s "
			"exceeds the limit (%u > %u)", sieve_binary_path(sbin),
			count, SIEVE_VARIABLES_MAX_SCOPE_SIZE );
		return FALSE;
	}

	*global_vars_r = sieve_variable_scope_create(&include_extension);
	pool = sieve_variable_scope_pool(*global_vars_r);

	/* Read global variable scope */
	for ( i = 0; i < count; i++ ) {
		struct sieve_variable *var;
		struct ext_include_variable *varctx;
		enum ext_include_variable_type type;
		string_t *identifier;

		if (
			!sieve_binary_read_byte(sbin, offset, &type) ||
			!sieve_binary_read_string(sbin, offset, &identifier) ) {
			/* Binary is corrupt, recompile */
			sieve_sys_error("include: failed to read global variable specification "
				"from dependency block %d of binary %s", block, sieve_binary_path(sbin));
			return FALSE;
		}

		if ( type >= EXT_INCLUDE_VAR_INVALID ) {
			/* Binary is corrupt, recompile */
			sieve_sys_error("include: dependency block %d of binary %s "
				"reports invalid global variable type (id %d).",
				block, sieve_binary_path(sbin), type);
			return FALSE;
		}
		
		var = sieve_variable_scope_declare(*global_vars_r, str_c(identifier));

		i_assert( var != NULL );

		varctx = p_new(pool, struct ext_include_variable, 1);
		varctx->type = type;
		var->context = varctx;

		i_assert(var->index == i);
	}
	
	return TRUE;
}

bool ext_include_variables_dump
(struct sieve_dumptime_env *denv, struct sieve_variable_scope *global_vars)
{
	unsigned int size;
	struct sieve_variable *const *vars;

	i_assert(global_vars != NULL);

	vars = sieve_variable_scope_get_variables(global_vars, &size);

	if ( size > 0 ) {
		unsigned int i;

		sieve_binary_dump_sectionf(denv, "Global variables");
	
		for ( i = 0; i < size; i++ ) {
			struct ext_include_variable *varctx =
				(struct ext_include_variable *) vars[i]->context;

			sieve_binary_dumpf(denv, "%3d: %s '%s' \n", i, 
				varctx->type == EXT_INCLUDE_VAR_EXPORTED ? "export" : "import", 
				vars[i]->identifier);
		}	
	}

	return TRUE;
}
