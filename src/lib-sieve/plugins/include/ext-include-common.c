#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-script.h"
#include "sieve-ast.h"
#include "sieve-binary.h"
#include "sieve-commands.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

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

/* Interpreter context */

struct ext_include_interpreter_context {
	struct sieve_interpreter *interp;
	unsigned int nesting_level;
	struct sieve_script *script;
	unsigned int block_id;
	
	unsigned int inc_block_id;
	bool returned;
	struct ext_include_interpreter_context *parent;
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

void ext_include_binary_free(struct sieve_binary *sbin)
{
	struct ext_include_binary_context *binctx = 
		ext_include_get_binary_context(sbin);
	struct hash_iterate_context *hctx = 
		hash_iterate_init(binctx->included_scripts);
	void *key, *value;
		
	while ( hash_iterate(hctx, &key, &value) ) {
		struct _included_script *incscript = (struct _included_script *) value;
		
		sieve_script_unref(&incscript->script);
	}

	hash_iterate_deinit(&hctx);
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
	
	/* Unreferenced on binary_free */
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

/* Interpreter context management */

static struct ext_include_interpreter_context *
	ext_include_create_interpreter_context
(struct sieve_interpreter *interp, 
	struct ext_include_interpreter_context *parent, 
	struct sieve_script *script, unsigned int block_id)
{	
	struct ext_include_interpreter_context *ctx;

	pool_t pool = sieve_interpreter_pool(interp);
	ctx = p_new(pool, struct ext_include_interpreter_context, 1);
	ctx->parent = parent;
	ctx->interp = interp;
	ctx->script = script;
	ctx->block_id = block_id;
	if ( parent == NULL ) 
		ctx->nesting_level = 0;
	else
		ctx->nesting_level = parent->nesting_level + 1;
	
	return ctx;
}

static inline struct ext_include_interpreter_context *
	ext_include_get_interpreter_context
(struct sieve_interpreter *interp)
{
	return (struct ext_include_interpreter_context *)
		sieve_interpreter_extension_get_context(interp, ext_include_my_id);
}

static inline struct ext_include_interpreter_context *
	ext_include_initialize_interpreter_context
(struct sieve_interpreter *interp, 
	struct ext_include_interpreter_context *parent, 
	struct sieve_script *script, unsigned int block_id)
{
	struct ext_include_interpreter_context *ctx = 
		ext_include_create_interpreter_context(interp, parent, script, block_id);
		
	sieve_interpreter_extension_set_context(interp, ext_include_my_id, ctx);
	
	return ctx;
}

void ext_include_register_interpreter_context
(struct sieve_interpreter *interp)
{
	struct ext_include_interpreter_context *ctx = 
		ext_include_get_interpreter_context(interp);
	struct sieve_script *script = sieve_interpreter_script(interp);
	
	if ( ctx == NULL ) {
		ctx = ext_include_create_interpreter_context
			(interp, NULL, script, SBIN_SYSBLOCK_MAIN_PROGRAM);
		
		sieve_interpreter_extension_set_context
			(interp, ext_include_my_id, (void *) ctx);		
	}
}

/* Including a script during generation */

bool ext_include_generate_include
(struct sieve_generator *gentr, struct sieve_command_context *cmd,
	const char *script_path, const char *script_name, unsigned *blk_id_r)
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
		
	*blk_id_r = 0;
		
	/* Just to be sure: do not include more scripts when errors have occured 
	 * already. 
	 */
	if ( sieve_get_errors(ehandler) > 0 )
		return FALSE;
		
	/* Limit nesting level */
	if ( ctx->nesting_level >= EXT_INCLUDE_MAX_NESTING_LEVEL ) {
		sieve_command_generate_error
			(gentr, cmd, "cannot nest includes deeper than %d levels",
				EXT_INCLUDE_MAX_NESTING_LEVEL);
		return FALSE;
	}
	
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

	/* Get/create our context from the binary we are working on */
	binctx = ext_include_get_binary_context(sbin);
	
	/* Is the script already compiled into the current binary? */
	if ( !ext_include_script_is_included(binctx, script, &inc_block_id) )	{	
		/* No, allocate a new block in the binary and mark the script as included.
		 */
		inc_block_id = sieve_binary_block_create(sbin);
		ext_include_script_include(binctx, script, inc_block_id);
		
		/* Include list now holds a reference, so we can release it here safely */
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

		/* Generate 
		 *
		 * FIXME: It might not be a good idea to recurse code generation for 
		 * included scripts.
		 */
		this_block_id = sieve_binary_block_set_active(sbin, inc_block_id); 	
	 	subgentr = sieve_generator_create(ast, ehandler);			
		ext_include_initialize_generator_context(subgentr, ctx, script);
			
		if ( !sieve_generator_run(subgentr, &sbin) ) {
			sieve_command_generate_error(gentr, cmd, 
				"failed to generate code for included script '%s'", script_name);
	 		result = FALSE;
		}
				
		(void) sieve_binary_block_set_active(sbin, this_block_id); 	
		sieve_generator_free(&subgentr);
		
		/* Cleanup */
		sieve_ast_unref(&ast);		
	} else 
		/* Yes, aready compiled and included, so release script object right away */
		sieve_script_unref(&script);

	if ( result ) *blk_id_r = inc_block_id;
	
	return result;
}

/* Executing an included script during interpretation */

bool ext_include_execute_include
	(const struct sieve_runtime_env *renv, unsigned int block_id)
{
	int result = TRUE;
	struct ext_include_interpreter_context *ctx =
		ext_include_get_interpreter_context(renv->interp);

	if ( ctx->parent == NULL ) {
		struct ext_include_interpreter_context *curctx;
		struct sieve_error_handler *ehandler = 
			sieve_interpreter_get_error_handler(renv->interp);
		struct sieve_interpreter *subinterp;
		unsigned int this_block_id;
		bool interrupted = FALSE;	

		/* We are the top-level interpreter instance */	

		/* Create interpreter for top-level included script (first sub-interpreter) 
		 */
		subinterp = sieve_interpreter_create(renv->sbin, ehandler);			
		curctx = ext_include_initialize_interpreter_context
			(subinterp, ctx, NULL, block_id);
	
		/* Activate and start the top-level included script */
		this_block_id = sieve_binary_block_set_active(renv->sbin, block_id); 			
		result = ( sieve_interpreter_start
			(subinterp, renv->msgdata, renv->scriptenv, renv->result, &interrupted)
			== 1 );
		
		/* Included scripts can have includes of their own. This is not implemented
		 * recursively. Rather, the sub-interpreter interrupts and defers the 
		 * include to the top-level interpreter, which is here.
		 */
		if ( result && interrupted && !curctx->returned ) {
			while ( result ) {
				if ( ( (interrupted && curctx->returned) || (!interrupted) ) && 
					curctx->parent != NULL ) {
					
					/* Sub-interpreter executed return */
					
					/* Ascend interpreter stack */
					curctx = curctx->parent;
					sieve_interpreter_free(&subinterp);
					
					/* This is the top-most sub-interpreter, bail out */
					if ( curctx->parent == NULL ) break;
					
					/* Reactivate parent */
					(void) sieve_binary_block_set_active(renv->sbin, curctx->block_id);
					subinterp = curctx->interp; 	
					
					/* Continue parent */
					curctx->inc_block_id = 0;
					curctx->returned = FALSE;
					result = ( sieve_interpreter_continue(subinterp, &interrupted) == 1 );
				} else {
					if ( curctx->inc_block_id >= SBIN_SYSBLOCK_LAST ) {
						/* Sub-include requested */
						
						/* Create sub-interpreter */
						subinterp = sieve_interpreter_create(renv->sbin, ehandler);			
						curctx = ext_include_initialize_interpreter_context
							(subinterp, curctx, NULL, curctx->inc_block_id);
													
						/* Activate the sub-include's block */
						(void) sieve_binary_block_set_active(renv->sbin, curctx->block_id);
						
						/* Start the sub-include's interpreter */
						curctx->inc_block_id = 0;
						curctx->returned = FALSE;
						result = ( sieve_interpreter_start
							(subinterp, renv->msgdata, renv->scriptenv, renv->result, 
								&interrupted) == 1 );		 	
					} else {
						/* Sub-interpreter was interrupted outside this extension, probably
						 * stop command was executed. Generate an interrupt ourselves, 
						 * ending all script execution.
						 */
						sieve_interpreter_interrupt(renv->interp);
						break;
					}
				}
			}
		}
		
		/* Free any sub-interpreters that might still be active */
		while ( curctx != NULL && curctx->parent != NULL ) {
			sieve_interpreter_free(&curctx->interp);
			curctx = curctx->parent;
		}

		/* Return to our own block */
		(void) sieve_binary_block_set_active(renv->sbin, this_block_id); 	
	} else {
		/* We are an included script already, defer inclusion to main interpreter */
		ctx->inc_block_id = block_id;
		sieve_interpreter_interrupt(renv->interp);	
	}
	
	return result;
}
