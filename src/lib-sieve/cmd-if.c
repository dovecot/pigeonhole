#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-code.h"
#include "sieve-binary.h"

/* Predeclarations */

static bool cmd_if_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_elsif_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);
static bool cmd_if_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);
static bool cmd_else_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx);

/* If command
 *
 * Syntax:   
 *   if <test1: test> <block1: block>
 */
const struct sieve_command cmd_if = { 
	"if", 
	SCT_COMMAND, 
	0, 1, TRUE, TRUE,
	NULL, NULL,
	cmd_if_validate, 
	cmd_if_generate, 
	NULL 
};

/* ElsIf command
 *
 * Santax:
 *   elsif <test2: test> <block2: block>
 */
const struct sieve_command cmd_elsif = {
    "elsif", 
	SCT_COMMAND,
	0, 1, TRUE, TRUE, 
	NULL, NULL, 
	cmd_elsif_validate, 
	cmd_if_generate, 
	NULL 
};

/* Else command 
 *
 * Syntax:   
 *   else <block>
 */
const struct sieve_command cmd_else = {
    "else", 
	SCT_COMMAND, 
	0, 0, TRUE, TRUE,
	NULL, NULL,
	cmd_elsif_validate, 
	cmd_else_generate, 
	NULL 
};

/* Context */

struct cmd_if_context_data {
	struct cmd_if_context_data *previous;
	struct cmd_if_context_data *next;
	
	bool jump_generated;
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
	ctx_data->jump_generated = FALSE;
	
	if ( previous != NULL )
		previous->next = ctx_data;
	
	cmd->data = ctx_data;
}

/* Validation */

static bool cmd_if_validate
	(struct sieve_validator *validator ATTR_UNUSED, 
		struct sieve_command_context *cmd) 
{
	cmd_if_initialize_context_data(cmd, NULL);
	
	return TRUE;
}

static bool cmd_elsif_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd)
{
	struct sieve_command_context *prev_context = 
		sieve_command_prev_context(cmd);

	/* Check valid command placement */
	if ( prev_context == NULL ||
		( prev_context->command != &cmd_if &&
			prev_context->command != &cmd_elsif ) ) 
	{		
		sieve_command_validate_error(validator, cmd, 
			"the %s command must follow an if or elseif command", 
			cmd->command->identifier);
		return FALSE;
	}
	
	/* Previous command in this block is 'if' or 'elsif', so we can safely refer 
	 * to its context data 
	 */
	cmd_if_initialize_context_data(cmd, prev_context->data);

	return TRUE;
}

/* Code generation */

static void cmd_if_resolve_exit_jumps
	(struct sieve_binary *sbin, struct cmd_if_context_data *ctx_data) 
{
	struct cmd_if_context_data *if_ctx = ctx_data->previous;
	
	while ( if_ctx != NULL ) {
		if ( if_ctx->jump_generated ) 
			sieve_binary_resolve_offset(sbin, if_ctx->exit_jump);
		if_ctx = if_ctx->previous;	
	}
}

static bool cmd_if_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx)
{
	struct sieve_binary *sbin = cgenv->sbin;
	struct cmd_if_context_data *ctx_data = (struct cmd_if_context_data *) ctx->data;
	struct sieve_ast_node *test;
	struct sieve_jumplist jmplist;
	
	/* Prepare jumplist */
	sieve_jumplist_init_temp(&jmplist, sbin);
	
	/* Generate test condition */
	test = sieve_ast_test_first(ctx->ast_node);
	sieve_generate_test(cgenv, test, &jmplist, FALSE);
		
	/* Case true { } */
	sieve_generate_block(cgenv, ctx->ast_node);
	
	/* Are we the final command in this if-elsif-else structure? */
	if ( ctx_data->next != NULL ) {
		/* No, generate jump to end of if-elsif-else structure (resolved later) 
		 * This of course is not necessary if the {} block contains a command 
		 * like stop at top level that unconditionally exits the block already
		 * anyway. 
		 */
		if ( !sieve_command_block_exits_unconditionally(ctx) ) {
			sieve_operation_emit_code(sbin, &sieve_jmp_operation, -1);
			ctx_data->exit_jump = sieve_binary_emit_offset(sbin, 0);
			ctx_data->jump_generated = TRUE;
		}
	} else {
		/* Yes, Resolve previous exit jumps to this point */
		cmd_if_resolve_exit_jumps(sbin, ctx_data);
	}
	
	/* Case false ... (subsequent elsif/else commands might generate more) */
	sieve_jumplist_resolve(&jmplist);	
		
	return TRUE;
}

static bool cmd_else_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command_context *ctx)
{
	struct cmd_if_context_data *ctx_data = (struct cmd_if_context_data *) ctx->data;
	
	/* Else */
	sieve_generate_block(cgenv, ctx->ast_node);
		
	/* End: resolve all exit blocks */	
	cmd_if_resolve_exit_jumps(cgenv->sbin, ctx_data);
		
	return TRUE;
}

