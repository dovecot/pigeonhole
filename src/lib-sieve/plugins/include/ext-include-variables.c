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
 * Forward declarations
 */
 
 
/* 
 * AST context 
 */

struct ext_include_ast_context {
	struct sieve_variable_scope *import_vars;
	struct sieve_variable_scope *global_vars;
};

struct ext_include_ast_context *ext_include_create_ast_context
(struct sieve_ast *ast, struct sieve_ast *parent)
{	
	struct ext_include_ast_context *ctx;

	pool_t pool = sieve_ast_pool(ast);
	ctx = p_new(pool, struct ext_include_ast_context, 1);
	ctx->import_vars = sieve_variable_scope_create(pool);
	
	if ( parent != NULL ) {
		struct ext_include_ast_context *parent_ctx = 
			(struct ext_include_ast_context *)
			sieve_ast_extension_get_context(parent, ext_include_my_id);
		ctx->global_vars = ( parent_ctx == NULL ? NULL : parent_ctx->global_vars );
	}
	
	sieve_ast_extension_set_context(ast, ext_include_my_id, ctx);
	
	return ctx;
}

static inline struct ext_include_ast_context *
	ext_include_get_ast_context(struct sieve_ast *ast)
{
	struct ext_include_ast_context *ctx = (struct ext_include_ast_context *)
		sieve_ast_extension_get_context(ast, ext_include_my_id);
		
	if ( ctx == NULL ) {
		ctx = ext_include_create_ast_context(ast, NULL);	
	}
	
	return ctx;
}

/* 
 * Variable import-export
 */
 
bool ext_include_variable_import
(struct sieve_validator *valdtr, struct sieve_command_context *cmd, 
	const char *variable)
{
	struct ext_include_ast_context *ctx = 
		ext_include_get_ast_context(cmd->ast_node->ast);
	
	if ( ctx->global_vars == NULL || 
		sieve_variable_scope_get_variable(ctx->global_vars, variable, FALSE)
		== NULL ) 
	{
		sieve_command_validate_error(valdtr, cmd, 
			"importing unknown global variable '%s'", variable);
		return FALSE;
	}
	
	printf("VAR IMPORT: %s\n", variable);
	(void)sieve_variable_scope_declare(ctx->import_vars, variable);
	
	return TRUE;
}

bool ext_include_variable_export
(struct sieve_validator *valdtr, struct sieve_command_context *cmd, 
	const char *variable)
{
	struct sieve_ast *ast = cmd->ast_node->ast;
	struct ext_include_ast_context *ctx = ext_include_get_ast_context(ast);
	
	if ( sieve_variable_scope_get_variable(ctx->import_vars, variable, FALSE) 
		!= NULL ) {
		sieve_command_validate_error(valdtr, cmd, 
					"cannot export imported variable '%s'", variable);
		return FALSE;
	}
	
	if ( ctx->global_vars == NULL ) {
		pool_t pool = sieve_ast_pool(ast);
		ctx->global_vars = sieve_variable_scope_create(pool);
	}
		
	printf("VAR EXPORT: %s\n", variable);
		
	(void)sieve_variable_scope_declare(ctx->global_vars, variable);
	return TRUE;	
}


