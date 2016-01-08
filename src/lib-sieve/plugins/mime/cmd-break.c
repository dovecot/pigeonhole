/* Copyright (c) 2002-2016 Pigeonhole authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-binary.h"
#include "sieve-dump.h"

#include "ext-mime-common.h"

#include <ctype.h>

/* break
 *
 * Syntax:
 *   break [":name" <name: string>]
 *
 */

static bool cmd_break_registered
	(struct sieve_validator *valdtr, const struct sieve_extension *ext,
		struct sieve_command_registration *cmd_reg);
static bool cmd_break_pre_validate
	(struct sieve_validator *valdtr, struct sieve_command *cmd);
static bool cmd_break_validate
	(struct sieve_validator *valdtr, struct sieve_command *cmd);
static bool cmd_break_generate
	(const struct sieve_codegen_env *cgenv,
		struct sieve_command *ctx);

const struct sieve_command_def cmd_break = {
	.identifier = "break",
	.type = SCT_COMMAND,
	.positional_args = 0,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.registered = cmd_break_registered,
	.pre_validate = cmd_break_pre_validate,
	.validate = cmd_break_validate,
	.generate = cmd_break_generate,
};

/*
 * Tagged arguments
 */

/* Forward declarations */

static bool cmd_break_validate_name_tag
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
		struct sieve_command *cmd);

/* Argument objects */

static const struct sieve_argument_def break_name_tag = {
	.identifier = "name",
	.validate = cmd_break_validate_name_tag
};

/*
 * Break operation
 */

static bool cmd_break_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_break_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def break_operation = {
	.mnemonic = "BREAK",
	.ext_def = &foreverypart_extension,
	.code = EXT_FOREVERYPART_OPERATION_BREAK,
	.dump = cmd_break_operation_dump,
	.execute = cmd_break_operation_execute
};

/*
 * Validation data
 */

struct cmd_break_data {
	struct sieve_ast_argument *name;
	struct sieve_command *loop_cmd;
};

/*
 * Tag validation
 */

static bool cmd_break_validate_name_tag
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
    struct sieve_command *cmd)
{
	struct cmd_break_data *data =
		(struct cmd_break_data *)cmd->data;
	struct sieve_ast_argument *tag = *arg;

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg, 1);

	/* Check syntax:
	 *   :name <string>
	 */
	if ( !sieve_validate_tag_parameter
		(valdtr, cmd, tag, *arg, NULL, 0, SAAT_STRING, TRUE) )
		return FALSE;
	data->name = *arg;

	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);
	return TRUE;
}

/*
 * Command registration
 */

static bool cmd_break_registered
(struct sieve_validator *valdtr, const struct sieve_extension *ext,
	struct sieve_command_registration *cmd_reg)
{
	sieve_validator_register_tag
		(valdtr, cmd_reg, ext, &break_name_tag, 0);
	
	return TRUE;
}

/*
 * Command validation
 */

static bool cmd_break_pre_validate
(struct sieve_validator *valdtr ATTR_UNUSED, struct sieve_command *cmd)
{
	struct cmd_break_data *data;
	pool_t pool = sieve_command_pool(cmd);
	
	data = p_new(pool, struct cmd_break_data, 1);
	cmd->data = data;
	return TRUE;
}

static bool cmd_break_validate
(struct sieve_validator *valdtr, struct sieve_command *cmd)
{
	struct cmd_break_data *data =
		(struct cmd_break_data *)cmd->data;
	struct sieve_ast_node *node = cmd->ast_node;
	const char *name =	( data->name == NULL ?
		NULL : sieve_ast_argument_strc(data->name) );

	i_assert(node != NULL);
	while ( node != NULL && node->command != NULL ) {
		if ( sieve_command_is(node->command, cmd_foreverypart) ) {
			struct ext_foreverypart_loop *loop =
				(struct ext_foreverypart_loop *)node->command->data;
			if ( name == NULL ||
				(name != NULL && loop->name != NULL &&
					strcmp(name, loop->name) == 0) ) {
				data->loop_cmd = node->command;
				break;
			}
		}
		node = sieve_ast_node_parent(node);
	}

	if ( data->loop_cmd == NULL ) {
		if ( name == NULL ) {
			sieve_command_validate_error(valdtr, cmd,
				"the break command is not placed inside "
				"a foreverypart loop");
		} else {
			sieve_command_validate_error(valdtr, cmd,
				"the break command is not placed inside "
				"a foreverypart loop named `%s'",
				name);
		}
		return FALSE;
	}

	sieve_command_exit_block_unconditionally(cmd);
	return TRUE;
}

/*
 * Code generation
 */

static bool cmd_break_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd)
{
	struct cmd_break_data *data =
		(struct cmd_break_data *)cmd->data;
	struct ext_foreverypart_loop *loop;

	i_assert( data->loop_cmd != NULL );
	loop = (struct ext_foreverypart_loop *)data->loop_cmd->data;

	sieve_operation_emit(cgenv->sblock, cmd->ext, &break_operation);
	sieve_jumplist_add(loop->exit_jumps,
		sieve_binary_emit_offset(cgenv->sblock, 0));
	return TRUE;
}

/*
 * Code dump
 */

static bool cmd_break_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	unsigned int pc = *address;
	sieve_offset_t offset;

	sieve_code_dumpf(denv, "BREAK");
	sieve_code_descend(denv);

 	if ( !sieve_binary_read_offset(denv->sblock, address, &offset) )
		return FALSE;

	sieve_code_dumpf(denv, "END: %d [%08x]", offset, pc + offset);
	return TRUE;
}

/*
 * Code execution
 */

static int cmd_break_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	struct sieve_interpreter_loop *loop;
	unsigned int pc = *address;
	sieve_offset_t offset;
	sieve_size_t loop_end;

	/*
	 * Read operands
	 */

	if ( !sieve_binary_read_offset(renv->sblock, address, &offset) )
	{
		sieve_runtime_trace_error(renv, "invalid loop end offset");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	loop_end = pc + offset;

	/*
	 * Perform operation
	 */

	sieve_runtime_trace(renv, SIEVE_TRLVL_ACTIONS, "break command");
	sieve_runtime_trace_descend(renv);

	loop = sieve_interpreter_loop_get
		(renv->interp, loop_end, &foreverypart_extension);
	if ( loop == NULL ) {
		sieve_runtime_trace_error(renv, "no matching loop found");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	sieve_interpreter_loop_break(renv->interp, loop);
	return SIEVE_EXEC_OK;
}


