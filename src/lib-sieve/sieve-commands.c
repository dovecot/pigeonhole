#include <stdio.h>

#include "sieve-ast.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-binary.h"

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-interpreter.h"

/* Default arguments implemented in this file */

static bool arg_number_generate
(struct sieve_generator *generator, struct sieve_ast_argument *arg, 
	struct sieve_command_context *context);
static bool arg_string_generate
(struct sieve_generator *generator, struct sieve_ast_argument *arg, 
	struct sieve_command_context *context);
static bool arg_string_list_generate
(struct sieve_generator *generator, struct sieve_ast_argument *arg, 
	struct sieve_command_context *context);

const struct sieve_argument number_argument =
	{ "@number", NULL, NULL, NULL, arg_number_generate };
const struct sieve_argument string_argument =
	{ "@string", NULL, NULL, NULL, arg_string_generate };
const struct sieve_argument string_list_argument =
	{ "@string-list", NULL, NULL, NULL, arg_string_list_generate };	

static bool arg_number_generate
(struct sieve_generator *generator, struct sieve_ast_argument *arg, 
	struct sieve_command_context *context ATTR_UNUSED)
{
	struct sieve_binary *sbin = sieve_generator_get_binary(generator);

	if ( sieve_ast_argument_type(arg) != SAAT_NUMBER ) {
		return FALSE;
	}
	
	sieve_opr_number_emit(sbin, sieve_ast_argument_number(arg));

	return TRUE;
}

static bool arg_string_generate
(struct sieve_generator *generator, struct sieve_ast_argument *arg, 
	struct sieve_command_context *context ATTR_UNUSED)
{
	struct sieve_binary *sbin = sieve_generator_get_binary(generator);

	if ( sieve_ast_argument_type(arg) != SAAT_STRING ) {
		return FALSE;
	} 

	sieve_opr_string_emit(sbin, sieve_ast_argument_str(arg));
  
	return TRUE;
}

static void emit_string_list_operand
	(struct sieve_generator *generator, const struct sieve_ast_argument *strlist)
{	
	struct sieve_binary *sbin = sieve_generator_get_binary(generator);
	void *list_context;
	const struct sieve_ast_argument *stritem;
  
	t_push();
  
	sieve_opr_stringlist_emit_start
		(sbin, sieve_ast_strlist_count(strlist), &list_context);

	stritem = sieve_ast_strlist_first(strlist);
	while ( stritem != NULL ) {
		sieve_opr_stringlist_emit_item
			(sbin, list_context, sieve_ast_strlist_str(stritem));
		stritem = sieve_ast_strlist_next(stritem);
	}

	sieve_opr_stringlist_emit_end(sbin, list_context);

	t_pop();
}

static bool arg_string_list_generate
(struct sieve_generator *generator, struct sieve_ast_argument *arg, 
	struct sieve_command_context *context ATTR_UNUSED)
{
	struct sieve_binary *sbin = sieve_generator_get_binary(generator);

	if ( sieve_ast_argument_type(arg) == SAAT_STRING ) {
		sieve_opr_string_emit(sbin, sieve_ast_argument_str(arg));
		
		return TRUE;
		
	} else if ( sieve_ast_argument_type(arg) == SAAT_STRING_LIST ) {
		if ( sieve_ast_strlist_count(arg) == 1 ) 
			sieve_opr_string_emit(sbin, 
				sieve_ast_argument_str(sieve_ast_strlist_first(arg)));
		else
			(void) emit_string_list_operand(generator, arg);
		
		return TRUE;
	}
	
	return FALSE;
}

/* Trivial tests implemented in this file */

static bool tst_false_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *context ATTR_UNUSED,
		struct sieve_jumplist *jumps, bool jump_true);
static bool tst_true_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED,
		struct sieve_jumplist *jumps, bool jump_true);

static const struct sieve_command tst_false = { 
	"false", 
	SCT_TEST, 
	0, 0, FALSE, FALSE,
	NULL, NULL, NULL, NULL, 
	tst_false_generate 
};

static const struct sieve_command tst_true = { 
	"true", 
	SCT_TEST, 
	0, 0, FALSE, FALSE,
	NULL, NULL, NULL, NULL, 
	tst_true_generate 
};

/* Trivial commands implemented in this file */

static bool cmd_stop_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED);
static bool cmd_stop_validate
	(struct sieve_validator *validator, struct sieve_command_context *ctx);
	
static bool cmd_keep_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED);
static bool cmd_discard_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED); 

static const struct sieve_command cmd_stop = { 
	"stop", 
	SCT_COMMAND, 
	0, 0, FALSE, FALSE,
	NULL, NULL,  
	cmd_stop_validate, 
	cmd_stop_generate, 
	NULL 
};

static const struct sieve_command cmd_keep = { 
	"keep", 
	SCT_COMMAND, 
	0, 0, FALSE, FALSE,
	NULL, NULL, NULL, 
	cmd_keep_generate, 
	NULL
};

static const struct sieve_command cmd_discard = { 
	"discard", 
	SCT_COMMAND, 
	0, 0, FALSE, FALSE,
	NULL, NULL, NULL, 
	cmd_discard_generate, 
	NULL 
};

/* Lists of core tests and commands */

const struct sieve_command *sieve_core_tests[] = {
	&tst_false, &tst_true,

	&tst_address, &tst_header, &tst_exists, &tst_size, 	
	&tst_not, &tst_anyof, &tst_allof
};

const unsigned int sieve_core_tests_count = N_ELEMENTS(sieve_core_tests);

const struct sieve_command *sieve_core_commands[] = {
	&cmd_stop, &cmd_keep, &cmd_discard,

	&cmd_require, &cmd_if, &cmd_elsif, &cmd_else, &cmd_redirect
};

const unsigned int sieve_core_commands_count = N_ELEMENTS(sieve_core_commands);
	
/* Command context */

inline struct sieve_command_context *sieve_command_prev_context	
	(struct sieve_command_context *context) 
{
	struct sieve_ast_node *node = sieve_ast_node_prev(context->ast_node);
	
	if ( node != NULL ) {
		return node->context;
	}
	
	return NULL;
}

inline struct sieve_command_context *sieve_command_parent_context	
	(struct sieve_command_context *context) 
{
	struct sieve_ast_node *node = sieve_ast_node_parent(context->ast_node);
	
	if ( node != NULL ) {
		return node->context;
	}
	
	return NULL;
}

struct sieve_command_context *sieve_command_context_create
	(struct sieve_ast_node *cmd_node, const struct sieve_command *command)
{
	struct sieve_command_context *cmd;
	
	cmd = p_new(sieve_ast_node_pool(cmd_node), struct sieve_command_context, 1);
	
	cmd->ast_node = cmd_node;	
	cmd->command = command;
	
	cmd->block_exit_command = NULL;
	
	return cmd;
}

const char *sieve_command_type_name(const struct sieve_command *command) {
	switch ( command->type ) {
	case SCT_NONE: return "command of unspecified type (bug)";
	case SCT_TEST: return "test";
	case SCT_COMMAND: return "command";
	default:
		break;
	}
	return "??COMMAND-TYPE??";
}

inline void sieve_command_exit_block_unconditionally
	(struct sieve_command_context *cmd)
{
	struct sieve_command_context *parent = sieve_command_parent_context(cmd);

	/* Only the first unconditional exit is of importance */
	if ( parent != NULL && parent->block_exit_command == NULL ) 
		parent->block_exit_command = cmd;
}

inline bool sieve_command_block_exits_unconditionally
	(struct sieve_command_context *cmd)
{
	return ( cmd->block_exit_command != NULL );
}

/* Code generation for trivial commands and tests */

static bool cmd_stop_validate
	(struct sieve_validator *validator ATTR_UNUSED, 
		struct sieve_command_context *ctx)
{
	sieve_command_exit_block_unconditionally(ctx);
	
	return TRUE;
}

static bool cmd_stop_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED) 
{
	sieve_operation_emit_code(
		sieve_generator_get_binary(generator), SIEVE_OPCODE_STOP);
	return TRUE;
}

static bool cmd_keep_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED) 
{
	sieve_operation_emit_code(
        sieve_generator_get_binary(generator), SIEVE_OPCODE_KEEP);
	return TRUE;
}

static bool cmd_discard_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED) 
{
	sieve_operation_emit_code(
        sieve_generator_get_binary(generator), SIEVE_OPCODE_DISCARD);
	return TRUE;
}

static bool tst_false_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *context ATTR_UNUSED,
		struct sieve_jumplist *jumps, bool jump_true)
{
	struct sieve_binary *sbin = sieve_generator_get_binary(generator);

	if ( !jump_true ) {
		sieve_operation_emit_code(sbin, SIEVE_OPCODE_JMP);
		sieve_jumplist_add(jumps, sieve_binary_emit_offset(sbin, 0));
	}
	
	return TRUE;
}

static bool tst_true_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED,
		struct sieve_jumplist *jumps, bool jump_true)
{
	struct sieve_binary *sbin = sieve_generator_get_binary(generator);

	if ( jump_true ) {
		sieve_operation_emit_code(sbin, SIEVE_OPCODE_JMP);
		sieve_jumplist_add(jumps, sieve_binary_emit_offset(sbin, 0));
	}
	
	return TRUE;
}

