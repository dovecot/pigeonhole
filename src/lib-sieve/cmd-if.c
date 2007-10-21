#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-code.h"

/* Context */

struct cmd_if_context_data {
	struct cmd_if_context_data *previous;
	struct cmd_if_context_data *next;
	
	sieve_size_t exit_jump;
};

static void cmd_if_initialize_context_data
	(struct sieve_command_context *cmd, struct cmd_if_context_data *previous) 
{ 	
	struct cmd_if_context_data *ctx_data;

	/* Assign context */
	ctx_data = p_new(sieve_command_pool(cmd), struct cmd_if_context_data, 1);
	ctx_data->previous = previous;
	ctx_data->next = NULL;
	ctx_data->exit_jump = 0;
	
	if ( previous != NULL )
		previous->next = ctx_data;
	
	cmd->data = ctx_data;
}

/* Validation */

static bool cmd_if_check_syntax(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{ 	
	/* Check valid syntax: 
	 *      Syntax:   if <test1: test> <block1: block>
	 *      Syntax:   elsif <test2: test> <block2: block>
	 */
	if ( !sieve_validate_command_arguments(validator, cmd, 0, NULL) ||
	 	!sieve_validate_command_subtests(validator, cmd, 1) || 
	 	!sieve_validate_command_block(validator, cmd, TRUE, TRUE) ) {
	 	
		return FALSE;
	}
	
	return TRUE;
}

static bool cmd_else_check_placement(struct sieve_validator *validator, struct sieve_command_context *cmd)
{
	/* Check valid command placement */
	if ( sieve_ast_command_prev(cmd->ast_node) == NULL ||
			( !sieve_ast_prev_cmd_is(cmd->ast_node, "if") &&
				!sieve_ast_prev_cmd_is(cmd->ast_node, "elsif") ) ) {
		
		sieve_command_validate_error(validator, cmd, 
			"the %s command must follow an if or elseif command", cmd->command->identifier);
		return FALSE;
	}
	
	return TRUE;
}

bool cmd_if_validate(struct sieve_validator *validator, struct sieve_command_context *cmd) 
{
	/* Check valid parameter syntax */
	if ( !cmd_if_check_syntax(validator, cmd) ) return FALSE;
	
	cmd_if_initialize_context_data(cmd, NULL);
	
	return TRUE;
}

bool cmd_elsif_validate(struct sieve_validator *validator __attr_unused__, struct sieve_command_context *cmd)
{
	struct sieve_command_context *prev_context;

	/* Check valid command placement */
	if ( !cmd_else_check_placement(validator, cmd) ) return FALSE;
	
	/* Check valid parameter syntax */
	if ( !cmd_if_check_syntax(validator, cmd) ) return FALSE;

	/* Previous command in this block is 'if' or 'elsif', so we can safely refer to its context data */
	prev_context = sieve_command_prev_context(cmd);
	i_assert( prev_context != NULL ); 
	
	cmd_if_initialize_context_data(cmd, prev_context->data);

	return TRUE;
}

bool cmd_else_validate(struct sieve_validator *validator __attr_unused__, struct sieve_command_context *cmd) 
{		
	struct sieve_command_context *prev_context;
	
	/* Check valid command placement */
	if ( !cmd_else_check_placement(validator, cmd) ) return FALSE;
	
	/* Check valid parameter syntax: 
	 *   Syntax:   else <block>
	 */
	if ( !sieve_validate_command_arguments(validator, cmd, 0, NULL) ||
	 	!sieve_validate_command_subtests(validator, cmd, 0) || 
	 	!sieve_validate_command_block(validator, cmd, TRUE, TRUE) ) {
	 	
		return FALSE;
	}
	
	/* Previous command in this block is 'if' or 'elsif', so we can safely refer to its context data */
	prev_context = sieve_command_prev_context(cmd);
	i_assert( prev_context != NULL ); 
	
	cmd_if_initialize_context_data(cmd, prev_context->data);
	
	return TRUE;
}

/* Code generation */

static void cmd_if_resolve_exit_jumps(struct sieve_generator *generator, struct cmd_if_context_data *ctx_data) 
{
	struct cmd_if_context_data *if_ctx = ctx_data->previous;
	
	while ( if_ctx != NULL ) {
		sieve_generator_resolve_offset(generator, if_ctx->exit_jump);
		if_ctx = if_ctx->previous;	
	}
}

bool cmd_if_generate
	(struct sieve_generator *generator, struct sieve_command_context *ctx)
{
	struct cmd_if_context_data *ctx_data = (struct cmd_if_context_data *) ctx->data;
	struct sieve_ast_node *test;
  struct sieve_jumplist jmplist;
	
	/* Prepare jumplist */
	sieve_jumplist_init(&jmplist);
	
	/* Generate test condition */
	test = sieve_ast_test_first(ctx->ast_node);
	sieve_generate_test(generator, test, &jmplist, FALSE);
		
	/* Case true { } */
	sieve_generate_block(generator, ctx->ast_node);
	
	/* Are we the final command in this if-elsif-else structure? */
	if ( ctx_data->next != NULL ) {
		/* No, generate jump to end of if-elsif-else structure (resolved later) */
		sieve_generator_emit_core_opcode(generator, SIEVE_OPCODE_JMP);
		ctx_data->exit_jump = sieve_generator_emit_offset(generator, 0);
	} else {
		/* Yes, Resolve previous exit jumps to this point */
		cmd_if_resolve_exit_jumps(generator, ctx_data);
	}
	
	/* Case false ... (subsequent elsif/else commands might generate more) */
  sieve_jumplist_resolve(&jmplist, generator);	
		
	return TRUE;
}

bool cmd_else_generate
	(struct sieve_generator *generator, struct sieve_command_context *ctx)
{
	struct cmd_if_context_data *ctx_data = (struct cmd_if_context_data *) ctx->data;
	
  /* Else */
	sieve_generate_block(generator, ctx->ast_node);
		
	/* End: resolve all exit blocks */	
	cmd_if_resolve_exit_jumps(generator, ctx_data);
		
	return TRUE;
}

