#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-script.h"
#include "sieve-ast.h"
#include "sieve-commands.h"
#include "sieve-generator.h"

#include "ext-include-common.h"

/* Generator context management */

static struct ext_include_main_context *ext_include_create_main_context
(struct sieve_generator *gentr)
{
	pool_t pool = sieve_generator_pool(gentr);
	
	struct ext_include_main_context *ctx = 
		p_new(pool, struct ext_include_main_context, 1);
	
	ctx->generator = gentr;
	ctx->included_scripts = hash_create
		(pool, pool, 0, str_hash, (hash_cmp_callback_t *)strcmp);
	
	return ctx;
}

static struct ext_include_generator_context *
	ext_include_create_generator_context
(struct sieve_generator *gentr, struct ext_include_generator_context *parent, 
	struct sieve_script *script)
{	
	struct ext_include_generator_context *ctx;

	pool_t pool = sieve_generator_pool(gentr);
	ctx = p_new(pool, struct ext_include_generator_context, 1);
	ctx->parent = parent;
	ctx->script = script;
	if ( parent == NULL ) {
		ctx->nesting_level = 0;
		ctx->main = ext_include_create_main_context(gentr);
	} else {
		ctx->nesting_level = parent->nesting_level + 1;
		ctx->main = parent->main;
	}
	
	return ctx;
}

inline struct ext_include_generator_context *ext_include_get_generator_context
(struct sieve_generator *gentr)
{
	return (struct ext_include_generator_context *)
		sieve_generator_extension_get_context(gentr, ext_include_my_id);
}

void ext_include_register_generator_context(struct sieve_generator *gentr)
{
	struct ext_include_generator_context *ctx = 
		ext_include_get_generator_context(gentr);
	struct sieve_script *script = sieve_generator_script(gentr);
	
	if ( ctx == NULL ) {
		ctx = ext_include_create_generator_context(gentr, NULL, script);
		
		sieve_generator_extension_set_context
			(gentr, ext_include_my_id, (void *) ctx);		
	}
}

bool ext_include_generate_include
(struct sieve_generator *gentr, struct sieve_command_context *cmd,
	const char *script_path, const char *script_name)
{
	bool result = TRUE;
	struct sieve_script *script;
	struct sieve_ast *ast;
	struct ext_include_generator_context *parent =
		ext_include_get_generator_context(gentr);
	struct ext_include_generator_context *ctx;
	struct sieve_error_handler *ehandler = sieve_generator_error_handler(gentr);
		
	/* Do not include more scripts when errors have occured already. */
	if ( sieve_get_errors(ehandler) > 0 )
		return FALSE;
	
	/* Create script object */
	if ( (script = sieve_script_create(script_path, script_name, ehandler)) 
		== NULL )
		return FALSE;
	
	/* Check for circular include */
	
	ctx = parent;
	while ( ctx != NULL ) {
		if ( sieve_script_equals(ctx->script, script) ) {
			sieve_command_generate_error(gentr, cmd, "circular include");
				
			sieve_script_unref(&script);
			return FALSE;
		}
		
		ctx = ctx->parent;
	}	
  	
	/* Parse */
	if ( (ast = sieve_parse(script, ehandler)) == NULL ) {
 		sieve_command_generate_error(gentr, cmd, 
 			"failed to parse included script '%s'", script_name);
		return NULL;
	}

	/* Validate */
	if ( !sieve_validate(ast, ehandler) ) {
		sieve_command_generate_error(gentr, cmd, 
			"failed to validate included script '%s'", script_name);
		
 		sieve_ast_unref(&ast);
 		return NULL;
 	}
 	
	sieve_ast_unref(&ast); 	 	
		
	return result;
}

