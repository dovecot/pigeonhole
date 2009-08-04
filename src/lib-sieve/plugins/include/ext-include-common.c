/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "str-sanitize.h"
#include "home-expand.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-script.h"
#include "sieve-ast.h"
#include "sieve-binary.h"
#include "sieve-commands.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-include-common.h"
#include "ext-include-limits.h"
#include "ext-include-binary.h"
#include "ext-include-variables.h"

#include <stdlib.h>

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

struct ext_include_interpreter_global {
	ARRAY_DEFINE(included_scripts, struct sieve_script *);

	struct sieve_variable_storage *variables;
};

struct ext_include_interpreter_context {
	struct ext_include_interpreter_context *parent;
	struct ext_include_interpreter_global *global;

	struct sieve_interpreter *interp;
	pool_t pool;

	unsigned int nesting_level;

	struct sieve_script *script;
	const struct ext_include_script_info *script_info;
	
	const struct ext_include_script_info *include;
	bool returned;
};

/* 
 * Script access 
 */

const char *ext_include_get_script_directory
(enum ext_include_script_location location, const char *script_name)
{
	const char *home, *sieve_dir;

	switch ( location ) {
	case EXT_INCLUDE_LOCATION_PERSONAL:
		sieve_dir = getenv("SIEVE_DIR");
		home = getenv("HOME");

		if (sieve_dir == NULL) {
			if ( home == NULL )	{		
				sieve_sys_error(
					"include: sieve_dir and home not set for :personal script include "	
					"(wanted script %s)", str_sanitize(script_name, 80));
				return NULL;
			}

			sieve_dir = "~/sieve"; 
		}

		if ( home != NULL )
			sieve_dir = home_expand_tilde(sieve_dir, home);	

		break;
   	case EXT_INCLUDE_LOCATION_GLOBAL:
		sieve_dir = getenv("SIEVE_GLOBAL_DIR");

		if (sieve_dir == NULL) {
			sieve_sys_error(
				"include: sieve_global_dir not set for :global script include "	
				"(wanted script %s)", str_sanitize(script_name, 80));
			return NULL;
		}

		break;
	default:
		return NULL;
	}


	return sieve_dir;
}

/*
 * AST context management
 */

static void ext_include_ast_free
(struct sieve_ast *ast ATTR_UNUSED, void *context)
{
	struct ext_include_ast_context *actx = 
		(struct ext_include_ast_context *) context;
	struct sieve_script **scripts;
	unsigned int count, i;

	/* Unreference included scripts */
	scripts = array_get_modifiable(&actx->included_scripts, &count);
	for ( i = 0; i < count; i++ ) {
		sieve_script_unref(&scripts[i]);
	}	

	/* Unreference variable scopes */
	if ( actx->global_vars != NULL )
		sieve_variable_scope_unref(&actx->global_vars);
}

static const struct sieve_ast_extension include_ast_extension = {
	&include_extension,
	ext_include_ast_free
};

struct ext_include_ast_context *ext_include_create_ast_context
(struct sieve_ast *ast, struct sieve_ast *parent)
{
	struct ext_include_ast_context *actx;

	pool_t pool = sieve_ast_pool(ast);
	actx = p_new(pool, struct ext_include_ast_context, 1);
	p_array_init(&actx->included_scripts, pool, 32);

	if ( parent != NULL ) {
		struct ext_include_ast_context *parent_ctx =
			(struct ext_include_ast_context *)
				sieve_ast_extension_get_context(parent, &include_extension);
		actx->global_vars = parent_ctx->global_vars;

		i_assert( actx->global_vars != NULL );

		sieve_variable_scope_ref(actx->global_vars);
	} else
		actx->global_vars = sieve_variable_scope_create(&include_extension);			

	sieve_ast_extension_register(ast, &include_ast_extension, (void *) actx);

	return actx;
}

struct ext_include_ast_context *ext_include_get_ast_context
(struct sieve_ast *ast)
{
	struct ext_include_ast_context *actx = (struct ext_include_ast_context *)
		sieve_ast_extension_get_context(ast, &include_extension);

	if ( actx != NULL ) return actx;

	return ext_include_create_ast_context(ast, NULL);
}

void ext_include_ast_link_included_script
(struct sieve_ast *ast, struct sieve_script *script) 
{
	struct ext_include_ast_context *actx = ext_include_get_ast_context(ast);

	array_append(&actx->included_scripts, &script, 1);
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
		sieve_generator_extension_get_context(gentr, &include_extension);
}

static inline void ext_include_initialize_generator_context
(struct sieve_generator *gentr, struct ext_include_generator_context *parent, 
	struct sieve_script *script)
{
	sieve_generator_extension_set_context(gentr, &include_extension,
		ext_include_create_generator_context(gentr, parent, script));
}

void ext_include_register_generator_context
(const struct sieve_codegen_env *cgenv)
{
	struct ext_include_generator_context *ctx = 
		ext_include_get_generator_context(cgenv->gentr);
	
	/* Initialize generator context if necessary */
	if ( ctx == NULL ) {
		ctx = ext_include_create_generator_context(
			cgenv->gentr, NULL, cgenv->script);
		
		sieve_generator_extension_set_context
			(cgenv->gentr, &include_extension, (void *) ctx);		
	}

	/* Initialize ast context if necessary */
	(void)ext_include_get_ast_context(cgenv->ast);
	(void)ext_include_binary_init(cgenv->sbin, cgenv->ast);
}

/*
 * Runtime initialization
 */

static void ext_include_runtime_init
    (const struct sieve_runtime_env *renv, void *context)
{
	struct ext_include_interpreter_context *ctx = 
		(struct ext_include_interpreter_context *) context;

	if ( ctx->parent == NULL ) {
		ctx->global = p_new(ctx->pool, struct ext_include_interpreter_global, 1);
		ctx->global->variables = sieve_variable_storage_create
			(ctx->pool, ext_include_binary_get_global_scope(renv->sbin), 0);
		p_array_init(&ctx->global->included_scripts, ctx->pool, 10);
	} else {
		ctx->global = ctx->parent->global;
	}

	sieve_ext_variables_set_storage
		(renv->interp, ctx->global->variables, &include_extension);	
}

static struct sieve_interpreter_extension include_interpreter_extension = {
	&include_extension,
	ext_include_runtime_init,
	NULL,
};

/* 
 * Interpreter context management 
 */

static struct ext_include_interpreter_context *
	ext_include_interpreter_context_create
(struct sieve_interpreter *interp, 
	struct ext_include_interpreter_context *parent, 
	struct sieve_script *script, const struct ext_include_script_info *sinfo)
{	
	struct ext_include_interpreter_context *ctx;

	pool_t pool = sieve_interpreter_pool(interp);
	ctx = p_new(pool, struct ext_include_interpreter_context, 1);
	ctx->pool = pool;
	ctx->parent = parent;
	ctx->interp = interp;
	ctx->script = script;
	ctx->script_info = sinfo;

	if ( parent == NULL ) {
		ctx->nesting_level = 0;
	} else {
		ctx->nesting_level = parent->nesting_level + 1;
	}

	return ctx;
}

static inline struct ext_include_interpreter_context *
	ext_include_get_interpreter_context
(struct sieve_interpreter *interp)
{
	return (struct ext_include_interpreter_context *)
		sieve_interpreter_extension_get_context(interp, &include_extension);
}

static inline struct ext_include_interpreter_context *
	ext_include_interpreter_context_init_child
(struct sieve_interpreter *interp, 
	struct ext_include_interpreter_context *parent, 
	struct sieve_script *script, const struct ext_include_script_info *sinfo)
{
	struct ext_include_interpreter_context *ctx = 
		ext_include_interpreter_context_create(interp, parent, script, sinfo);
		
	sieve_interpreter_extension_register
		(interp, &include_interpreter_extension, ctx);
	
	return ctx;
}

void ext_include_interpreter_context_init
(struct sieve_interpreter *interp)
{
	struct ext_include_interpreter_context *ctx = 
		ext_include_get_interpreter_context(interp);

	/* Is this is the top-level interpreter ? */	
	if ( ctx == NULL ) {
		struct sieve_script *script;

		/* Initialize top context */
		script = sieve_interpreter_script(interp);
		ctx = ext_include_interpreter_context_create
			(interp, NULL, script, NULL);
		
		sieve_interpreter_extension_register
			(interp, &include_interpreter_extension, (void *) ctx);			
	}
}

struct sieve_variable_storage *ext_include_interpreter_get_global_variables
(struct sieve_interpreter *interp)
{
	struct ext_include_interpreter_context *ctx =
		ext_include_get_interpreter_context(interp);
		
	return ctx->global->variables;
}

/* 
 * Including a script during code generation 
 */

bool ext_include_generate_include
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *cmd,
	enum ext_include_script_location location, struct sieve_script *script, 
	const struct ext_include_script_info **included_r, bool once)
{
	bool result = TRUE;
	struct sieve_ast *ast;
	struct sieve_binary *sbin = cgenv->sbin;
	struct sieve_generator *gentr = cgenv->gentr;
	struct ext_include_binary_context *binctx;
	struct sieve_generator *subgentr;
	struct ext_include_generator_context *ctx =
		ext_include_get_generator_context(gentr);
	struct ext_include_generator_context *pctx;
	struct sieve_error_handler *ehandler = sieve_generator_error_handler(gentr);
	const struct ext_include_script_info *included;
		
	*included_r = NULL;

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
	if ( !once ) {
		pctx = ctx;
		while ( pctx != NULL ) {
			if ( sieve_script_equals(pctx->script, script) ) {
				sieve_command_generate_error(gentr, cmd, "circular include");
				
				return FALSE;
			}
		
			pctx = pctx->parent;
		}
	}

	/* Get binary context */
	binctx = ext_include_binary_init(sbin, cgenv->ast);

	/* Is the script already compiled into the current binary? */
	if ( !ext_include_binary_script_is_included(binctx, script, &included) )	
	{	
		unsigned int inc_block_id, this_block_id;
		const char *script_name = sieve_script_name(script);

		/* Check whether include limit is exceeded */
		if ( ext_include_binary_script_get_count(binctx) >= 
			EXT_INCLUDE_MAX_INCLUDES ) {
	 		sieve_command_generate_error(gentr, cmd, 
	 			"failed to include script '%s': no more than %u includes allowed", 
				str_sanitize(script_name, 80), EXT_INCLUDE_MAX_INCLUDES);
	 		return FALSE;			
		}
		
		/* No, allocate a new block in the binary and mark the script as included.
		 */
		inc_block_id = sieve_binary_block_create(sbin);
		included = ext_include_binary_script_include
			(binctx, script, location, inc_block_id);

		/* Parse */
		if ( (ast = sieve_parse(script, ehandler)) == NULL ) {
	 		sieve_command_generate_error(gentr, cmd, 
	 			"failed to parse included script '%s'", str_sanitize(script_name, 80));
	 		return FALSE;
		}
		
		/* Included scripts inherit global variable scope */
		(void)ext_include_create_ast_context(ast, cmd->ast_node->ast);

		/* Validate */
		if ( !sieve_validate(ast, ehandler) ) {
			sieve_command_generate_error(gentr, cmd, 
				"failed to validate included script '%s'", str_sanitize(script_name, 80));
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
					"failed to generate code for included script '%s'", 
					str_sanitize(script_name, 80));
		 		result = FALSE;
			}
			
			if ( sbin != NULL )		
				(void) sieve_binary_block_set_active(sbin, this_block_id, NULL); 	
			sieve_generator_free(&subgentr);
		} else {
			sieve_sys_error("include: failed to activate binary  block %d for "
				"generating code for the included script", inc_block_id);
			result = FALSE;
		}
		
		/* Cleanup */
		sieve_ast_unref(&ast);		
	} 

	if ( result ) *included_r = included;
	
	return result;
}

/* 
 * Executing an included script during interpretation 
 */

static int ext_include_runtime_check_circular
(struct ext_include_interpreter_context *ctx,
	const struct ext_include_script_info *include)
{
	struct ext_include_interpreter_context *pctx;

	pctx = ctx;
	while ( pctx != NULL ) {

		if ( sieve_script_equals(include->script, pctx->script) )
			return TRUE;

		pctx = pctx->parent;
	}

	return FALSE;
}

static bool ext_include_runtime_include_mark
(struct ext_include_interpreter_context *ctx,
	const struct ext_include_script_info *include, bool once)
{
	struct sieve_script *const *includes;
	unsigned int count, i;
	
	includes = array_get(&ctx->global->included_scripts, &count);
	for ( i = 0; i < count; i++ )	{
		if ( sieve_script_equals(include->script, includes[i]) )
			return ( !once );
	}
	
	array_append(&ctx->global->included_scripts, &include->script, 1);

	return TRUE;
}

bool ext_include_execute_include
(const struct sieve_runtime_env *renv, unsigned int include_id, bool once)
{
	int result = TRUE;
	struct ext_include_interpreter_context *ctx;
	const struct ext_include_script_info *included;
	struct ext_include_binary_context *binctx = 
		ext_include_binary_get_context(renv->sbin);

	/* Check for invalid include id (== corrupt binary) */
	included = ext_include_binary_script_get_included(binctx, include_id);
	if ( included == NULL ) {
		sieve_runtime_trace_error(renv, "invalid include id: %d", include_id);
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	ctx = ext_include_get_interpreter_context(renv->interp);
	
	sieve_runtime_trace(renv, 
		"INCLUDE command (script: %s, id: %d block: %d) START::", 
		sieve_script_name(included->script), include_id, included->block_id);

	/* If :once modifier is specified, check for duplicate include */
	if ( !ext_include_runtime_include_mark(ctx, included, once) ) {
		/* skip */

		sieve_runtime_trace(renv, 
			"INCLUDE command (block: %d) SKIPPED ::", included->block_id);
		return result;
	}

	/* Check circular include during interpretation as well. 
	 * Let's not trust binaries.
	 */
	if ( ext_include_runtime_check_circular(ctx, included) ) {
		sieve_runtime_trace_error(renv, 
			"circular include for script: %s [%d]", 
			sieve_script_name(included->script), included->block_id);

		/* Situation has no valid way to emerge at runtime */
		return SIEVE_EXEC_BIN_CORRUPT; 
	}

	if ( ctx->parent == NULL ) {
		struct ext_include_interpreter_context *curctx = NULL;
		struct sieve_error_handler *ehandler = 
			sieve_interpreter_get_error_handler(renv->interp);
		struct sieve_interpreter *subinterp;
		unsigned int this_block_id;
		bool interrupted = FALSE;	

		/* We are the top-level interpreter instance */	
		
		/* Activate block for included script */
		if ( !sieve_binary_block_set_active
			(renv->sbin, included->block_id, &this_block_id) ) {			
			sieve_runtime_trace_error(renv, "invalid block id: %d", 
				included->block_id);
			result = SIEVE_EXEC_BIN_CORRUPT;
		}

		if ( result > 0 ) {
			/* Create interpreter for top-level included script
			 * (first sub-interpreter) 
			 */
			subinterp = sieve_interpreter_create(renv->sbin, ehandler);

			if ( subinterp != NULL ) {			
				curctx = ext_include_interpreter_context_init_child
					(subinterp, ctx, included->script, included);

				/* Activate and start the top-level included script */
				result = ( sieve_interpreter_start
					(subinterp, renv->msgdata, renv->scriptenv, renv->result, 
						&interrupted) == 1 );
			} else
				result = SIEVE_EXEC_BIN_CORRUPT;
		}
		
		/* Included scripts can have includes of their own. This is not implemented
		 * recursively. Rather, the sub-interpreter interrupts and defers the 
		 * include to the top-level interpreter, which is here.
		 */
		if ( result > 0 && interrupted && !curctx->returned ) {
			while ( result > 0 ) {

				if ( ( (interrupted && curctx->returned) || (!interrupted) ) && 
					curctx->parent != NULL ) {
					
					/* Sub-interpreter ended or executed return */
					
					sieve_runtime_trace(renv, "INCLUDE command (block: %d) END ::", 
						curctx->script_info->block_id);

					/* Ascend interpreter stack */
					curctx = curctx->parent;
					sieve_interpreter_free(&subinterp);
					
					/* This is the top-most sub-interpreter, bail out */
					if ( curctx->parent == NULL ) break;
					
					/* Reactivate parent */
					(void) sieve_binary_block_set_active
						(renv->sbin, curctx->script_info->block_id, NULL);
					subinterp = curctx->interp; 	
					
					/* Continue parent */
					curctx->include = NULL;
					curctx->returned = FALSE;

					result = ( sieve_interpreter_continue(subinterp, &interrupted) == 1 );
				} else {
					if ( curctx->include != NULL ) {

						/* Sub-include requested */
															
						/* Activate the sub-include's block */
						if ( !sieve_binary_block_set_active
							(renv->sbin, curctx->include->block_id, NULL) ) {
							sieve_runtime_trace_error(renv, "invalid block id: %d", 
								curctx->include->block_id);
							result = SIEVE_EXEC_BIN_CORRUPT;
						}
				
						if ( result > 0 ) {
							/* Create sub-interpreter */
							subinterp = sieve_interpreter_create(renv->sbin, ehandler);			

							if ( subinterp != NULL ) {
								curctx = ext_include_interpreter_context_init_child
									(subinterp, curctx, curctx->include->script, 
										curctx->include);

								/* Start the sub-include's interpreter */
								curctx->include = NULL;
								curctx->returned = FALSE;
								result = ( sieve_interpreter_start
									(subinterp, renv->msgdata, renv->scriptenv, renv->result, 
										&interrupted) == 1 );		 	
							} else
								result = SIEVE_EXEC_BIN_CORRUPT;
						}
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
		} else 
			sieve_runtime_trace(renv, "INCLUDE command (block: %d) END ::", 
				curctx->script_info->block_id);

		/* Free any sub-interpreters that might still be active */
		while ( curctx != NULL && curctx->parent != NULL ) {
			struct ext_include_interpreter_context *nextctx	= curctx->parent;
			struct sieve_interpreter *killed_interp = curctx->interp;

			/* This kills curctx too */
			sieve_interpreter_free(&killed_interp);

			/* Luckily we recorded the parent earlier */
			curctx = nextctx;
		}

		/* Return to our own block */
		(void) sieve_binary_block_set_active(renv->sbin, this_block_id, NULL); 	
	} else {
		/* We are an included script already, defer inclusion to main interpreter */

		ctx->include = included;
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
	
