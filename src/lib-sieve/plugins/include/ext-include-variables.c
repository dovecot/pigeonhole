/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
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
 * Variable import-export
 */
 
struct sieve_variable *ext_include_variable_import_global
(struct sieve_validator *valdtr, struct sieve_command_context *cmd, 
	const char *variable)
{
	struct sieve_ast *ast = cmd->ast_node->ast;
	struct ext_include_ast_context *ctx = ext_include_get_ast_context(ast);
	struct sieve_variable_scope *main_scope;
	struct sieve_variable *var = NULL;

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
	
	/* Import the global variable into the local script scope */
	main_scope = sieve_ext_variables_get_main_scope(valdtr);
	(void)sieve_variable_scope_import(main_scope, var);

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
		string_t *identifier;

		if ( !sieve_binary_read_string(sbin, offset, &identifier) ) {
			/* Binary is corrupt, recompile */
			sieve_sys_error("include: failed to read global variable specification "
				"from dependency block %d of binary %s", block, sieve_binary_path(sbin));
			return FALSE;
		}
		
		var = sieve_variable_scope_declare(*global_vars_r, str_c(identifier));

		i_assert( var != NULL );
		i_assert( var->index == i );
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
			sieve_binary_dumpf(denv, "%3d: '%s' \n", i, vars[i]->identifier);
		}	
	}

	return TRUE;
}
