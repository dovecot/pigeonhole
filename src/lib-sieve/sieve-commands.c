/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "sieve-ast.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-binary.h"

#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-interpreter.h"

/* 
 * Literal arguments
 */

/* Forward declarations */

static bool arg_number_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
		struct sieve_command_context *context);
static bool arg_string_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
		struct sieve_command_context *context);
static bool arg_string_list_validate
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
		struct sieve_command_context *context);
static bool arg_string_list_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
		struct sieve_command_context *context);

/* Argument objects */

const struct sieve_argument number_argument = { 
	"@number", 
	NULL, NULL, NULL, NULL,
	arg_number_generate 
};

const struct sieve_argument string_argument = { 
	"@string", 
	NULL, NULL, NULL, NULL,
	arg_string_generate 
};

const struct sieve_argument string_list_argument = { 
	"@string-list", 
	NULL, NULL,
	arg_string_list_validate, 
	NULL, 
	arg_string_list_generate 
};	

/* Argument implementations */

static bool arg_number_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
	struct sieve_command_context *context ATTR_UNUSED)
{
	sieve_opr_number_emit(cgenv->sbin, sieve_ast_argument_number(arg));

	return TRUE;
}

static bool arg_string_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
	struct sieve_command_context *context ATTR_UNUSED)
{
	sieve_opr_string_emit(cgenv->sbin, sieve_ast_argument_str(arg));
  
	return TRUE;
}

static bool arg_string_list_validate
(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *context)
{
	struct sieve_ast_argument *stritem;

	stritem = sieve_ast_strlist_first(*arg);	
	while ( stritem != NULL ) {
		if ( !sieve_validator_argument_activate(validator, context, stritem, FALSE) )
			return FALSE;
			
		stritem = sieve_ast_strlist_next(stritem);
	}

	return TRUE;	
}

static bool emit_string_list_operand
(const struct sieve_codegen_env *cgenv, const struct sieve_ast_argument *strlist,
	struct sieve_command_context *context)
{	
	void *list_context;
	struct sieve_ast_argument *stritem;
   	
	sieve_opr_stringlist_emit_start
		(cgenv->sbin, sieve_ast_strlist_count(strlist), &list_context);

	stritem = sieve_ast_strlist_first(strlist);
	while ( stritem != NULL ) {
		if ( !sieve_generate_argument(cgenv, stritem, context) )
			return FALSE;
			
		stritem = sieve_ast_strlist_next(stritem);
	}

	sieve_opr_stringlist_emit_end(cgenv->sbin, list_context);
	
	return TRUE;
}

static bool arg_string_list_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
	struct sieve_command_context *context)
{
	if ( sieve_ast_argument_type(arg) == SAAT_STRING ) {
		return ( sieve_generate_argument(cgenv, arg, context) );

	} else if ( sieve_ast_argument_type(arg) == SAAT_STRING_LIST ) {
		bool result = TRUE;
		
		if ( sieve_ast_strlist_count(arg) == 1 ) 
			return ( sieve_generate_argument
				(cgenv, sieve_ast_strlist_first(arg), context) );
		else {
			T_BEGIN { 
				result=emit_string_list_operand(cgenv, arg, context);
			} T_END;
		}

		return result;
	}
	
	return FALSE;
}

/* 
 * Core tests and commands 
 */

const struct sieve_command *sieve_core_tests[] = {
	&tst_false, &tst_true,
	&tst_not, &tst_anyof, &tst_allof,
	&tst_address, &tst_header, &tst_exists, &tst_size
};

const unsigned int sieve_core_tests_count = N_ELEMENTS(sieve_core_tests);

const struct sieve_command *sieve_core_commands[] = {
	&cmd_require, 
	&cmd_stop, &cmd_if, &cmd_elsif, &cmd_else, 
	&cmd_keep, &cmd_discard, &cmd_redirect
};

const unsigned int sieve_core_commands_count = N_ELEMENTS(sieve_core_commands);
	
/* 
 * Command context 
 */

struct sieve_command_context *sieve_command_prev_context	
	(struct sieve_command_context *context) 
{
	struct sieve_ast_node *node = sieve_ast_node_prev(context->ast_node);
	
	if ( node != NULL ) {
		return node->context;
	}
	
	return NULL;
}

struct sieve_command_context *sieve_command_parent_context	
	(struct sieve_command_context *context) 
{
	struct sieve_ast_node *node = sieve_ast_node_parent(context->ast_node);
	
	if ( node != NULL ) {
		return node->context;
	}
	
	return NULL;
}

struct sieve_command_context *sieve_command_context_create
	(struct sieve_ast_node *cmd_node, const struct sieve_command *command,
		struct sieve_command_registration *reg)
{
	struct sieve_command_context *cmd;
	
	cmd = p_new(sieve_ast_node_pool(cmd_node), struct sieve_command_context, 1);
	
	cmd->ast_node = cmd_node;	
	cmd->command = command;
	cmd->cmd_reg = reg;
	
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

struct sieve_ast_argument *sieve_command_add_dynamic_tag
(struct sieve_command_context *cmd, const struct sieve_argument *tag, 
	int id_code)
{
	struct sieve_ast_argument *arg;
	
	if ( cmd->first_positional != NULL )
		arg = sieve_ast_argument_tag_insert
			(cmd->first_positional, tag->identifier, cmd->ast_node->source_line);
	else
		arg = sieve_ast_argument_tag_create
			(cmd->ast_node, tag->identifier, cmd->ast_node->source_line);
	
	arg->argument = tag;
	arg->arg_id_code = id_code;
	
	return arg;
}

struct sieve_ast_argument *sieve_command_find_argument
(struct sieve_command_context *cmd, const struct sieve_argument *argument)
{
	struct sieve_ast_argument *arg = sieve_ast_argument_first(cmd->ast_node);
		
	/* Visit tagged and optional arguments */
	while ( arg != NULL ) {
		if ( arg->argument == argument ) 
			return arg;
			
		arg = sieve_ast_argument_next(arg);
	}
	
	return arg;
}

/* Use this function with caution. The command commits to exiting the block.
 * When it for some reason does not, the interpretation will break later on, 
 * because exiting jumps are not generated when they would otherwise be 
 * necessary.
 */
void sieve_command_exit_block_unconditionally
	(struct sieve_command_context *cmd)
{
	struct sieve_command_context *parent = sieve_command_parent_context(cmd);

	/* Only the first unconditional exit is of importance */
	if ( parent != NULL && parent->block_exit_command == NULL ) 
		parent->block_exit_command = cmd;
}

bool sieve_command_block_exits_unconditionally
	(struct sieve_command_context *cmd)
{
	return ( cmd->block_exit_command != NULL );
}
