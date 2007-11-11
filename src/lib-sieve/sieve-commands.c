#include <stdio.h>

#include "sieve-ast.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-binary.h"

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-interpreter.h"

/* Default arguments implemented in this file */

static bool arg_number_generate(struct sieve_generator *generator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *context);
static bool arg_string_generate(struct sieve_generator *generator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *context);
static bool arg_string_list_generate(struct sieve_generator *generator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *context);

const struct sieve_argument number_argument =
	{ "@number", NULL, arg_number_generate };
const struct sieve_argument string_argument =
	{ "@string", NULL, arg_string_generate };
const struct sieve_argument string_list_argument =
	{ "@string-list", NULL, arg_string_list_generate };	

static bool arg_number_generate(struct sieve_generator *generator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *context ATTR_UNUSED)
{
	struct sieve_binary *sbin = sieve_generator_get_binary(generator);

	if ( sieve_ast_argument_type(*arg) != SAAT_NUMBER ) {
		return FALSE;
	}
	
	sieve_opr_number_emit(sbin, sieve_ast_argument_number(*arg));

	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

static bool arg_string_generate(struct sieve_generator *generator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *context ATTR_UNUSED)
{
	struct sieve_binary *sbin = sieve_generator_get_binary(generator);

	if ( sieve_ast_argument_type(*arg) != SAAT_STRING ) {
		return FALSE;
	} 

	sieve_opr_string_emit(sbin, sieve_ast_argument_str(*arg));
  
	*arg = sieve_ast_argument_next(*arg);
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

static bool arg_string_list_generate(struct sieve_generator *generator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *context ATTR_UNUSED)
{
	struct sieve_binary *sbin = sieve_generator_get_binary(generator);

	if ( sieve_ast_argument_type(*arg) == SAAT_STRING ) {
		sieve_opr_string_emit(sbin, sieve_ast_argument_str(*arg));
		
		*arg = sieve_ast_argument_next(*arg);
		return TRUE;
		
	} else if ( sieve_ast_argument_type(*arg) == SAAT_STRING_LIST ) {
		if ( sieve_ast_strlist_count(*arg) == 1 ) 
			sieve_opr_string_emit(sbin, 
				sieve_ast_argument_str(sieve_ast_strlist_first(*arg)));
		else
			(void) emit_string_list_operand(generator, *arg);
		
		*arg = sieve_ast_argument_next(*arg);
		return TRUE;
	}
	
	return FALSE;
}

/* Trivial commands implemented in this file */

const struct sieve_command sieve_core_tests[] = {
	{ "false", SCT_TEST, NULL, NULL, NULL, tst_false_generate },
	{ "true", SCT_TEST, NULL, NULL, NULL, tst_true_generate },
	{ "address", SCT_TEST, tst_address_registered, tst_address_validate, tst_address_generate, NULL },
	{ "header", SCT_TEST, tst_header_registered, tst_header_validate, tst_header_generate, NULL },
	{ "exists", SCT_TEST, NULL, tst_exists_validate, tst_exists_generate, NULL },
	{ "size", SCT_TEST, tst_size_registered, tst_size_validate, tst_size_generate, NULL },
	{ "not", SCT_TEST, NULL, tst_not_validate, NULL, tst_not_generate },
	{ "anyof", SCT_TEST, NULL, tst_anyof_validate, NULL, tst_anyof_generate },
	{ "allof", SCT_TEST, NULL, tst_allof_validate, NULL, tst_allof_generate }
};

const unsigned int sieve_core_tests_count =
	(sizeof(sieve_core_tests) / sizeof(sieve_core_tests[0]));

const struct sieve_command sieve_core_commands[] = {
	{ "stop", SCT_COMMAND, NULL, NULL, cmd_stop_generate, NULL },
	{ "keep", SCT_COMMAND, NULL, NULL, cmd_keep_generate, NULL},
	{ "discard", SCT_COMMAND, NULL, NULL, cmd_discard_generate, NULL },
	{ "require", SCT_COMMAND, NULL, cmd_require_validate, cmd_require_generate, NULL },
	{ "if", SCT_COMMAND, NULL, cmd_if_validate, cmd_if_generate, NULL },
	{ "elsif", SCT_COMMAND, NULL, cmd_elsif_validate, cmd_if_generate, NULL },
	{ "else", SCT_COMMAND, NULL, cmd_else_validate, cmd_else_generate, NULL },
	{ "redirect", SCT_COMMAND, NULL, cmd_redirect_validate, NULL, NULL }
};

const unsigned int sieve_core_commands_count =
	(sizeof(sieve_core_commands) / sizeof(sieve_core_commands[0]));
	
struct sieve_command_context *sieve_command_prev_context	
	(struct sieve_command_context *context) 
{
	struct sieve_ast_node *node = sieve_ast_node_prev(context->ast_node);
	
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
	
	return cmd;
}

const char *sieve_command_type_name(const struct sieve_command *command) {
	switch ( command->type ) {
	case SCT_TEST: return "test";
	case SCT_COMMAND: return "command";
	default:
		i_unreached();
	}
	return "??COMMAND-TYPE??";
}

/* Code generation for trivial commands and tests */
bool cmd_stop_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED) 
{
	sieve_operation_emit_code(
		sieve_generator_get_binary(generator), SIEVE_OPCODE_STOP);
	return TRUE;
}

bool cmd_keep_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED) 
{
	sieve_operation_emit_code(
        sieve_generator_get_binary(generator), SIEVE_OPCODE_KEEP);
	return TRUE;
}

bool cmd_discard_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED) 
{
	sieve_operation_emit_code(
        sieve_generator_get_binary(generator), SIEVE_OPCODE_DISCARD);
	return TRUE;
}

bool tst_false_generate
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

bool tst_true_generate
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

