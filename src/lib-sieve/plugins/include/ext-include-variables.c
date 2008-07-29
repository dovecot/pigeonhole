#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-ast.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-ext-variables.h"

#include "ext-include-common.h"
#include "ext-include-variables.h"

/* 
 * Variable import-export
 */
 
bool ext_include_variable_import_global
(struct sieve_validator *valdtr, struct sieve_command_context *cmd, 
	const char *variable, bool export)
{
	struct sieve_ast *ast = cmd->ast_node->ast;
	struct ext_include_ast_context *ctx = ext_include_get_ast_context(ast);
	struct sieve_variable_scope *main_scope;
	struct sieve_variable *var;

	if ( export ) {	
		if ( sieve_variable_scope_get_variable(ctx->import_vars, variable, FALSE) 
			!= NULL ) {
			sieve_command_validate_error(valdtr, cmd, 
				"cannot export imported variable '%s'", variable);
			return FALSE;
		}
	
		if ( ctx->global_vars == NULL ) {
			pool_t pool = sieve_ast_pool(ast);
			ctx->global_vars = sieve_variable_scope_create(pool, &include_extension);
		}
		
		var = sieve_variable_scope_get_variable(ctx->global_vars, variable, TRUE);
		
	} else {
		var = NULL;
		
		if ( ctx->global_vars != NULL ) {
			var = sieve_variable_scope_get_variable
				(ctx->global_vars, variable, FALSE);
		}
	
		if ( var == NULL ) {
			sieve_command_validate_error(valdtr, cmd, 
				"importing unknown global variable '%s'", variable);
			return FALSE;
		}
		
		(void)sieve_variable_scope_declare(ctx->import_vars, variable);
	}

	main_scope = sieve_ext_variables_get_main_scope(valdtr);
	(void)sieve_variable_scope_import(main_scope, var);
	return TRUE;	
}


