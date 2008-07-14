#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-script.h"
#include "sieve-ast.h"
#include "sieve-binary.h"
#include "sieve-commands.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-include-common.h"
#include "ext-include-binary.h"
#include "ext-include-variables.h"

/*
 * Forward declarations
 */
 
/* Generator context */

struct ext_include_generator_context {
	unsigned int nesting_level;
	struct sieve_script *script;
	struct ext_include_generator_context *parent;
};

static inline struct ext_include_generator_context *
	ext_include_get_generator_context
	(struct sieve_generator *gentr);

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

/* 
 * Script access 
 */

#define HARDCODED_PERSONAL_DIR "src/lib-sieve/plugins/include/"
#define HARDCODED_GLOBAL_DIR "src/lib-sieve/plugins/include/"

const char *ext_include_get_script_path
(enum ext_include_script_location location, const char *script_name)
{
	/* FIXME: Hardcoded */	
	switch ( location ) {
	case EXT_INCLUDE_LOCATION_PERSONAL:
		return t_strconcat(HARDCODED_PERSONAL_DIR, script_name, ".sieve", NULL);
	case EXT_INCLUDE_LOCATION_GLOBAL:
		return t_strconcat(HARDCODED_GLOBAL_DIR, script_name, ".sieve", NULL);
	default:
		break;
	}
	
	return NULL;
}

/* 
 * Generator context management 
 */
 
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
	} else {
		ctx->nesting_level = parent->nesting_level + 1;
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


/* 
 * Interpreter context management 
 */

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

void ext_include_interpreter_context_init
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

/* 
 * Including a script during generation 
 */

bool ext_include_generate_include
(struct sieve_generator *gentr, struct sieve_command_context *cmd,
	enum ext_include_script_location location, struct sieve_script *script, 
	unsigned *blk_id_r)
{
	bool result = TRUE;
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
	
	/* Check for circular include */
	pctx = ctx;
	while ( pctx != NULL ) {
		if ( sieve_script_equals(pctx->script, script) ) {
			sieve_command_generate_error(gentr, cmd, "circular include");
				
			return FALSE;
		}
		
		pctx = pctx->parent;
	}	

	/* Initialize binary context */
	binctx = ext_include_binary_init(sbin);

	/* Is the script already compiled into the current binary? */
	if ( !ext_include_binary_script_is_included(binctx, script, &inc_block_id) )	
	{	
		const char *script_name = sieve_script_name(script);
		
		/* No, allocate a new block in the binary and mark the script as included.
		 */
		inc_block_id = sieve_binary_block_create(sbin);
		ext_include_binary_script_include(binctx, script, location, inc_block_id);
		
		/* Parse */
		if ( (ast = sieve_parse(script, ehandler)) == NULL ) {
	 		sieve_command_generate_error(gentr, cmd, 
	 			"failed to parse included script '%s'", script_name);
	 		return FALSE;
		}
		
		/* Included scripts inherit global variable scope */
		(void)ext_include_create_ast_context(ast, cmd->ast_node->ast);

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
		if ( sieve_binary_block_set_active(sbin, inc_block_id, &this_block_id) ) {
		 	subgentr = sieve_generator_create(ast, ehandler);			
			ext_include_initialize_generator_context(subgentr, ctx, script);
				
			if ( !sieve_generator_run(subgentr, &sbin) ) {
				sieve_command_generate_error(gentr, cmd, 
					"failed to generate code for included script '%s'", script_name);
		 		result = FALSE;
			}
					
			(void) sieve_binary_block_set_active(sbin, this_block_id, NULL); 	
			sieve_generator_free(&subgentr);
		} else result = FALSE;
		
		/* Cleanup */
		sieve_ast_unref(&ast);		
	} 

	if ( result ) *blk_id_r = inc_block_id;
	
	return result;
}

/* 
 * Executing an included script during interpretation 
 */

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
		struct sieve_variable_storage *varstrg;
		unsigned int this_block_id;
		bool interrupted = FALSE;	

		/* We are the top-level interpreter instance */	

		/* Create interpreter for top-level included script (first sub-interpreter) 
		 */
		subinterp = sieve_interpreter_create(renv->sbin, ehandler);			
		curctx = ext_include_initialize_interpreter_context
			(subinterp, ctx, NULL, block_id);
			
		/* Create variable storage for global variables */
		varstrg = sieve_ext_variables_get_storage(renv->interp, ext_include_my_id);
		sieve_ext_variables_set_storage(subinterp, varstrg, ext_include_my_id);
	
		/* Activate and start the top-level included script */
		if ( sieve_binary_block_set_active(renv->sbin, block_id, &this_block_id) ) 			
			result = ( sieve_interpreter_start
				(subinterp, renv->msgdata, renv->scriptenv, renv->msgctx, renv->result, 
					&interrupted) == 1 );
		else
			result = FALSE;
		
		/* Included scripts can have includes of their own. This is not implemented
		 * recursively. Rather, the sub-interpreter interrupts and defers the 
		 * include to the top-level interpreter, which is here.
		 */
		if ( result && interrupted && !curctx->returned ) {
			while ( result ) {
				if ( ( (interrupted && curctx->returned) || (!interrupted) ) && 
					curctx->parent != NULL ) {
					
					/* Sub-interpreter ended or executed return */
					
					/* Ascend interpreter stack */
					curctx = curctx->parent;
					sieve_interpreter_free(&subinterp);
					
					/* This is the top-most sub-interpreter, bail out */
					if ( curctx->parent == NULL ) break;
					
					/* Reactivate parent */
					(void) sieve_binary_block_set_active
						(renv->sbin, curctx->block_id, NULL);
					subinterp = curctx->interp; 	
					
					/* Continue parent */
					curctx->inc_block_id = 0;
					curctx->returned = FALSE;
					result = ( sieve_interpreter_continue(subinterp, &interrupted) == 1 );
				} else {
					if ( curctx->inc_block_id >= SBIN_SYSBLOCK_LAST ) {
						/* Sub-include requested */
				
						/* FIXME: Check circular include during interpretation as well. 
						 * Let's not trust user-owned binaries.
						 */
						
						/* Create sub-interpreter */
						subinterp = sieve_interpreter_create(renv->sbin, ehandler);			
						curctx = ext_include_initialize_interpreter_context
							(subinterp, curctx, NULL, curctx->inc_block_id);
													
						/* Activate the sub-include's block */
						if ( sieve_binary_block_set_active
							(renv->sbin, curctx->block_id, NULL) ) {
							/* Start the sub-include's interpreter */
							curctx->inc_block_id = 0;
							curctx->returned = FALSE;
							result = ( sieve_interpreter_start
								(subinterp, renv->msgdata, renv->scriptenv, renv->msgctx,
									renv->result, &interrupted) == 1 );		 	
						} else 
							result = FALSE;
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
		(void) sieve_binary_block_set_active(renv->sbin, this_block_id, NULL); 	
	} else {
		/* We are an included script already, defer inclusion to main interpreter */
		ctx->inc_block_id = block_id;
		sieve_interpreter_interrupt(renv->interp);	
	}
	
	return result;
}

void ext_include_execute_return(const struct sieve_runtime_env *renv)
{
	struct ext_include_interpreter_context *ctx =
		ext_include_get_interpreter_context(renv->interp);
	
	ctx->returned = TRUE;
	sieve_interpreter_interrupt(renv->interp);	
}
	
