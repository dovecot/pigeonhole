#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-script.h"
#include "sieve-ast.h"
#include "sieve-binary.h"
#include "sieve-commands.h"
#include "sieve-generator.h"

#include "ext-include-common.h"

/* Generator context */

struct ext_include_generator_context {
	unsigned int nesting_level;
	struct sieve_script *script;
	struct ext_include_main_context *main;
	struct ext_include_generator_context *parent;
};

static inline struct ext_include_generator_context *
	ext_include_get_generator_context
	(struct sieve_generator *gentr);

/* Binary context */

struct ext_include_binary_context {
	struct sieve_binary *binary;
	struct hash_table *included_scripts;
};

/* Main context management */

static struct ext_include_main_context *ext_include_create_main_context
(struct sieve_generator *gentr)
{
	pool_t pool = sieve_generator_pool(gentr);
	
	struct ext_include_main_context *ctx = 
		p_new(pool, struct ext_include_main_context, 1);
	
	ctx->generator = gentr;
	
	return ctx;
}

/* Generator context management */

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

static inline struct ext_include_generator_context *
	ext_include_get_generator_context
(struct sieve_generator *gentr)
{
	return (struct ext_include_generator_context *)
		sieve_generator_extension_get_context(gentr, ext_include_my_id);
}

static inline void ext_include_initialize_generator_context
(struct sieve_generator *gentr, struct ext_include_generator_context *parent, 
	struct sieve_script *script)
{
	sieve_generator_extension_set_context(gentr, ext_include_my_id,
		ext_include_create_generator_context(gentr, parent, script));
}

void ext_include_register_generator_context
(struct sieve_generator *gentr)
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

/* Binary context functions */

struct _included_script {
	struct sieve_script *script;
	unsigned int block_id;
};

static struct ext_include_binary_context *ext_include_create_binary_context
(struct sieve_binary *sbin)
{
	pool_t pool = sieve_binary_pool(sbin);
	
	struct ext_include_binary_context *ctx = 
		p_new(pool, struct ext_include_binary_context, 1);
	
	ctx->binary = sbin;
	ctx->included_scripts = hash_create(pool, pool, 0, 
		(hash_callback_t *) sieve_script_hash, 
		(hash_cmp_callback_t *) sieve_script_cmp);
	
	return ctx;
}

static inline struct ext_include_binary_context *ext_include_get_binary_context
(struct sieve_binary *sbin)
{	
	struct ext_include_binary_context *ctx = (struct ext_include_binary_context *)
		sieve_binary_extension_get_context(sbin, ext_include_my_id);
	
	if ( ctx == NULL ) {
		ctx = ext_include_create_binary_context(sbin);
		sieve_binary_extension_set_context(sbin, ext_include_my_id, ctx);
	};
	
	return ctx;
}

static void ext_include_script_include
(struct ext_include_binary_context *binctx, struct sieve_script *script,
	unsigned int block_id)
{
	pool_t pool = sieve_binary_pool(binctx->binary);
	struct _included_script *incscript;
	
	incscript = p_new(pool, struct _included_script, 1);
	incscript->script = script;
	incscript->block_id = block_id;
	
	printf("INCLUDE: %s\n", sieve_script_path(script));
	
	/* FIXME: NOWW!!
	 *   THIS WILL CAUSE A MEMORY LEAK!!
	 */ 
	sieve_script_ref(script);
	
	hash_insert(binctx->included_scripts, (void *) script, (void *) incscript);
}

static bool ext_include_script_is_included
(struct ext_include_binary_context *binctx, struct sieve_script *script,
	unsigned int *block_id)
{
	struct _included_script *incscript = (struct _included_script *)
		hash_lookup(binctx->included_scripts, script);
		
	if ( incscript == 0 )
		return FALSE;
		
	printf("ALREADY INCLUDED: %s\n", sieve_script_path(incscript->script));
		
	*block_id = incscript->block_id;
	return TRUE;
}

/* Including a script during generation */

bool ext_include_generate_include
(struct sieve_generator *gentr, struct sieve_command_context *cmd,
	const char *script_path, const char *script_name)
{
	bool result = TRUE;
	struct sieve_script *script;
	struct sieve_ast *ast;
	struct sieve_binary *sbin = sieve_generator_get_binary(gentr);
	struct ext_include_binary_context *binctx;
	struct sieve_generator *subgentr;
	struct ext_include_generator_context *ctx =
		ext_include_get_generator_context(gentr);
	struct ext_include_generator_context *pctx;
	struct sieve_error_handler *ehandler = sieve_generator_error_handler(gentr);
	unsigned this_block_id, inc_block_id; 
		
	/* Do not include more scripts when errors have occured already. */
	if ( sieve_get_errors(ehandler) > 0 )
		return FALSE;
	
	/* Create script object */
	if ( (script = sieve_script_create(script_path, script_name, ehandler)) 
		== NULL )
		return FALSE;
	
	/* Check for circular include */
	
	pctx = ctx;
	while ( pctx != NULL ) {
		if ( sieve_script_equals(pctx->script, script) ) {
			sieve_command_generate_error(gentr, cmd, "circular include");
				
			sieve_script_unref(&script);
			return FALSE;
		}
		
		pctx = pctx->parent;
	}	

	binctx = ext_include_get_binary_context(sbin);
	
	/* Is the script already compiled into the current binary? */
	if ( !ext_include_script_is_included(binctx, script, &inc_block_id) )	{	
		/* Allocate a new block in the binary and mark the script as included 
		 * already.
		 */
		inc_block_id = sieve_binary_block_create(sbin);
		ext_include_script_include(binctx, script, inc_block_id);
		
		/* Include list now holds a reference */
		sieve_script_unref(&script);
		
		/* Parse */
		if ( (ast = sieve_parse(script, ehandler)) == NULL ) {
	 		sieve_command_generate_error(gentr, cmd, 
	 			"failed to parse included script '%s'", script_name);
	 		return FALSE;
		}

		/* Validate */
		if ( !sieve_validate(ast, ehandler) ) {
			sieve_command_generate_error(gentr, cmd, 
				"failed to validate included script '%s'", script_name);
	 		sieve_ast_unref(&ast);
	 		return FALSE;
	 	}

		/* Generate */
		this_block_id = sieve_binary_block_set_active(sbin, inc_block_id); 	
	 	subgentr = sieve_generator_create(ast, ehandler);			
		ext_include_initialize_generator_context(subgentr, ctx, script);
			
		if ( !sieve_generator_run(subgentr, &sbin) ) {
			sieve_command_generate_error(gentr, cmd, 
				"failed to validate included script '%s'", script_name);
	 		result = FALSE;
		}
				
		(void) sieve_binary_block_set_active(sbin, this_block_id); 	
		sieve_generator_free(&subgentr);
		
		/* Cleanup */
		sieve_ast_unref(&ast);		
	} else 
		sieve_script_unref(&script);
	
	return result;
}

