#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-ast.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-ext-variables.h"

#include "ext-include-common.h"
#include "ext-include-variables.h"

/*
 * Forward declarations
 */

/*
 * Variables scope
 */
 
struct ext_include_variables_scope {
	struct sieve_variable_scope *global_vars;
};

struct ext_include_variables_scope *ext_include_variables_scope_create
(pool_t pool)
{
	struct ext_include_variables_scope *scope = 
		p_new(pool, struct ext_include_variables_scope, 1);
	
	scope->global_vars = sieve_variable_scope_create(pool);
	
	return scope;
}

 
/* 
 * AST context 
 */

struct ext_include_ast_context {
	struct sieve_variable_scope *import_vars;
	struct sieve_variable_scope *export_vars;
};

static struct ext_include_ast_context *ext_include_create_ast_context
(struct sieve_ast *ast)
{	
	struct ext_include_ast_context *ctx;

	pool_t pool = sieve_ast_pool(ast);
	ctx = p_new(pool, struct ext_include_ast_context, 1);
	ctx->import_vars = sieve_variable_scope_create(pool);
	ctx->export_vars = sieve_variable_scope_create(pool);
	
	return ctx;
}

static inline struct ext_include_ast_context *
	ext_include_get_ast_context(struct sieve_ast *ast)
{
	struct ext_include_ast_context *ctx = (struct ext_include_ast_context *)
		sieve_ast_extension_get_context(ast, ext_include_my_id);
		
	if ( ctx == NULL ) {
		ctx = ext_include_create_ast_context(ast);
		sieve_ast_extension_set_context(ast, ext_include_my_id, ctx);
	}
	
	return ctx;
}

/* 
 * Variable import-export
 */
 
void ext_include_variable_import(struct sieve_ast *ast, const char *variable)
{
	struct ext_include_ast_context *ctx = ext_include_get_ast_context(ast);
	
	printf("VAR IMPORT: %s\n", variable);
	
	(void)sieve_variable_scope_declare(ctx->import_vars, variable);
}

bool ext_include_variable_export(struct sieve_ast *ast, const char *variable)
{
	struct ext_include_ast_context *ctx = ext_include_get_ast_context(ast);
	
	printf("VAR EXPORT: %s\n", variable);
	
	if ( sieve_variable_scope_get_variable(ctx->import_vars, variable, FALSE) 
		!= NULL )
		return FALSE;
		
	(void)sieve_variable_scope_declare(ctx->export_vars, variable);
	return TRUE;	
}


