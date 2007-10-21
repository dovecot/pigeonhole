#include "lib.h"

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-extensions.h"
#include "sieve-validator.h" 

bool cmd_require_validate(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{
	struct sieve_ast_argument *arg;
	
	/* Check valid command placement */
	if ( sieve_ast_node_type(sieve_ast_node_parent(cmd->ast_node)) != SAT_ROOT ||
		( sieve_ast_command_prev(cmd->ast_node) != NULL &&
			!sieve_ast_prev_cmd_is(cmd->ast_node, "require") ) ) {
		
		sieve_command_validate_error(validator, cmd, 
			"the require command can only be placed at top level at the beginning of the file");
		return FALSE;
	}
	
	/* Check valid syntax 
	 *   Syntax: require <capabilities: string-list>
	 */
	if ( !sieve_validate_command_arguments(validator, cmd, 1, &arg ) ||
	 	!sieve_validate_command_subtests(validator, cmd, 0) || 
	 	!sieve_validate_command_block(validator, cmd, FALSE, FALSE) ) {
	 	return FALSE;
	}
	
	cmd->data = arg;

	/* Check argument and load specified extension(s) */
	if ( sieve_ast_argument_type(arg) == SAAT_STRING )
		/* Single string */
		sieve_validator_load_extension(validator, cmd, sieve_ast_argument_strc(arg));
	else if ( sieve_ast_argument_type(arg) == SAAT_STRING_LIST ) {
		/* String list */
		struct sieve_ast_argument *stritem = sieve_ast_strlist_first(arg);
		
		while ( stritem != NULL ) {
			sieve_validator_load_extension(validator, cmd, sieve_ast_strlist_strc(stritem));
			
			stritem = sieve_ast_strlist_next(stritem);
		}
	} else {
		/* Something else */
		sieve_command_validate_error(validator, cmd, 
			"the require command accepts a single string or string list argument but %s was found", 
			sieve_ast_argument_name(arg));
		return FALSE;
	}
	 
	return TRUE;
}

bool cmd_require_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx) 
{
	struct sieve_ast_argument *arg = (struct sieve_ast_argument *) ctx->data;
	
	sieve_generator_emit_core_opcode(generator, SIEVE_OPCODE_LOAD);

	/* Emit  */  	
	if ( !sieve_generator_emit_stringlist_argument(generator, arg) ) 
		return FALSE;
	
	return TRUE;
}
