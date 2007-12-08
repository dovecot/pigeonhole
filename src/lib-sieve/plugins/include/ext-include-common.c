#include "sieve-common.h"
#include "sieve-script.h"
#include "sieve-ast.h"
#include "sieve-commands.h"
#include "sieve-error.h"
#include "sieve-validator.h"

#include "ext-include-common.h"

/* Validator context management */

static struct ext_include_main_context *ext_include_create_main_context
(struct sieve_validator *validator)
{
	pool_t pool = sieve_validator_pool(validator);
	
	struct ext_include_main_context *ctx = 
		p_new(pool, struct ext_include_main_context, 1);
	
	ctx->validator = validator;
	ctx->included_scripts = hash_create
		(pool, pool, 0, str_hash, (hash_cmp_callback_t *)strcmp);
	
	return ctx;
}

static struct ext_include_validator_context *
	ext_include_create_validator_context
(struct sieve_validator *validator, 
	struct ext_include_validator_context *parent, struct sieve_script *script)
{	
	struct ext_include_validator_context *ctx;

	pool_t pool = sieve_validator_pool(validator);
	ctx = p_new(pool, struct ext_include_validator_context, 1);
	ctx->parent = parent;
	ctx->script = script;
	if ( parent == NULL ) {
		ctx->nesting_level = 0;
		ctx->main = ext_include_create_main_context(validator);
	} else {
		ctx->nesting_level = parent->nesting_level + 1;
		ctx->main = parent->main;
	}
	
	return ctx;
}

inline struct ext_include_validator_context *
	ext_include_get_validator_context
(struct sieve_validator *validator)
{
	return (struct ext_include_validator_context *)
		sieve_validator_extension_get_context(validator, ext_include_my_id);
}

void ext_include_register_validator_context
(struct sieve_validator *validator, struct sieve_script *script)
{
	struct ext_include_validator_context *ctx = 
		ext_include_get_validator_context(validator);
	
	if ( ctx == NULL ) {
		ctx = ext_include_create_validator_context(validator, NULL, script);
		
		sieve_validator_extension_set_context
			(validator, ext_include_my_id, (void *) ctx);		
	}
}

bool ext_include_validate_include
(struct sieve_validator *validator, struct sieve_command_context *cmd,
	const char *script_path, const char *script_name, struct sieve_ast **ast_r)
{
	bool result = TRUE;
	struct sieve_script *script;
	struct sieve_validator *subvalid; 
	struct ext_include_validator_context *parent =
		ext_include_get_validator_context(validator);
	struct ext_include_validator_context *ctx;
	struct sieve_error_handler *ehandler = 
		sieve_validator_get_error_handler(validator);
	
	/* Create script object */
	if ( (script = sieve_script_create(script_path, script_name, ehandler)) 
		== NULL )
		return FALSE;
	
	*ast_r = NULL;
	
	/* Check for circular include */
	
	ctx = parent;
	while ( ctx != NULL ) {
		if ( sieve_script_equals(ctx->script, script) ) {
			sieve_command_validate_error
				(validator, cmd, "circular include");
				
			sieve_script_unref(&script);
			return FALSE;
		}
		
		ctx = ctx->parent;
	}	
			
	/* Parse script */
	
	if ( (*ast_r = sieve_parse(script, ehandler)) == NULL ) {
 		sieve_command_validate_error
 			(validator, cmd, "parse failed for included script '%s'", script_name);
		sieve_script_unref(&script);
		return FALSE;
	}
	
	/* AST now holds a reference, so we can drop it already */
	sieve_script_unref(&script);
	
	/* Validate script */

	subvalid = sieve_validator_create(*ast_r, ehandler);	
	ctx = ext_include_create_validator_context(subvalid, parent, script);
	sieve_validator_extension_set_context(subvalid, ext_include_my_id, ctx);		
		
	if ( !sieve_validator_run(subvalid) || sieve_get_errors(ehandler) > 0 ) {
		sieve_command_validate_error
			(validator, cmd, "validation failed for included script '%s'", 
				script_name);
		sieve_ast_unref(ast_r);
		result = FALSE;
	}
		
	sieve_validator_free(&subvalid);	
		
	return result;
}

