#include "lib.h"

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-extensions.h"
#include "sieve-validator.h" 

struct cmd_require_context_data {
	struct sieve_ast_argument *arg;
	struct sieve_extension *extension;
};

bool cmd_require_validate(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{
	bool result = TRUE;
	struct sieve_ast_argument *arg;
	
	/* Check valid command placement */
	if ( sieve_ast_node_type(sieve_ast_node_parent(cmd->ast_node)) != SAT_ROOT ||
		( sieve_ast_command_prev(cmd->ast_node) != NULL &&
			!sieve_ast_prev_cmd_is(cmd->ast_node, "require") ) ) {
		
		sieve_command_validate_error(validator, cmd, 
			"the require command can only be placed at top level "
			"at the beginning of the file");
		return FALSE;
	}
	
	/* Check valid syntax 
	 *   Syntax: require <capabilities: string-list>
	 */
	if ( !sieve_validate_command_arguments(validator, cmd, 1) ||
	 	!sieve_validate_command_subtests(validator, cmd, 0) || 
	 	!sieve_validate_command_block(validator, cmd, FALSE, FALSE) ) {
	 	return FALSE;
	}
	
	arg = cmd->first_positional;
	
	/* Check argument and load specified extension(s) */
	if ( sieve_ast_argument_type(arg) == SAAT_STRING ) {
		/* Single string */
		int ext_id = sieve_validator_extension_load
			(validator, cmd, sieve_ast_argument_strc(arg));	

		if ( ext_id < 0 ) result = FALSE;
		arg->context = (void *) ext_id;

	} else if ( sieve_ast_argument_type(arg) == SAAT_STRING_LIST ) {
		/* String list */
		struct sieve_ast_argument *stritem = sieve_ast_strlist_first(arg);
		
		while ( stritem != NULL ) {
			int ext_id = sieve_validator_extension_load
				(validator, cmd, sieve_ast_strlist_strc(stritem));

			if ( ext_id < 0 ) result = FALSE;
			stritem->context = (void *) ext_id;
	
			stritem = sieve_ast_strlist_next(stritem);
		}
	} else {
		/* Something else */
		sieve_command_validate_error(validator, cmd, 
			"the require command accepts a single string or string list argument, "
			"but %s was found", 
			sieve_ast_argument_name(arg));
		return FALSE;
	}
	 
	return result;
}

bool cmd_require_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx) 
{
	struct sieve_ast_argument *arg = ctx->first_positional;
	
	if ( sieve_ast_argument_type(arg) == SAAT_STRING ) {
		int ext_id = (int) arg->context;
		
		sieve_generator_link_extension(generator, ext_id);
	} else if ( sieve_ast_argument_type(arg) == SAAT_STRING_LIST ) {
		/* String list */
		struct sieve_ast_argument *stritem = sieve_ast_strlist_first(arg);
		
		while ( stritem != NULL ) {
			int ext_id = (int) stritem->context;
		
			sieve_generator_link_extension(generator, ext_id);
			
			stritem = sieve_ast_strlist_next(stritem);
		}
	} else {
		i_unreached();
	}
	
	return TRUE;
}
