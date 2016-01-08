/* Copyright (c) 2002-2016 Pigeonhole authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-binary.h"
#include "sieve-dump.h"
#include "sieve-message.h"

#include "ext-mime-common.h"

#include <ctype.h>

/* Foreverypart
 *
 * Syntax:
 *   foreverypart [":name" <name: string>] <block>
 *
 */

static bool cmd_foreverypart_registered
	(struct sieve_validator *valdtr, const struct sieve_extension *ext,
		struct sieve_command_registration *cmd_reg);
static bool cmd_foreverypart_pre_validate
	(struct sieve_validator *valdtr, struct sieve_command *cmd);
static bool cmd_foreverypart_validate
	(struct sieve_validator *valdtr, struct sieve_command *cmd);
static bool cmd_foreverypart_generate
	(const struct sieve_codegen_env *cgenv,
		struct sieve_command *ctx);

const struct sieve_command_def cmd_foreverypart = {
	.identifier = "foreverypart",
	.type = SCT_COMMAND,
	.positional_args = 0,
	.subtests = 0,
	.block_allowed = TRUE,
	.block_required = TRUE,
	.registered = cmd_foreverypart_registered,
	.pre_validate = cmd_foreverypart_pre_validate,
	.validate = cmd_foreverypart_validate,
	.generate = cmd_foreverypart_generate
};

/*
 * Tagged arguments
 */

/* Forward declarations */

static bool cmd_foreverypart_validate_name_tag
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
		struct sieve_command *cmd);

/* Argument objects */

static const struct sieve_argument_def foreverypart_name_tag = {
	.identifier = "name",
	.validate = cmd_foreverypart_validate_name_tag,
};

/*
 * foreverypart operation
 */

static bool cmd_foreverypart_begin_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_foreverypart_begin_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def foreverypart_begin_operation = {
	.mnemonic = "FOREVERYPART_BEGIN",
	.ext_def = &foreverypart_extension,
	.code = EXT_FOREVERYPART_OPERATION_FOREVERYPART_BEGIN,
	.dump = cmd_foreverypart_begin_operation_dump,
	.execute = cmd_foreverypart_begin_operation_execute
};

static bool cmd_foreverypart_end_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_foreverypart_end_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def foreverypart_end_operation = {
	.mnemonic = "FOREVERYPART_END",
	.ext_def = &foreverypart_extension,
	.code = EXT_FOREVERYPART_OPERATION_FOREVERYPART_END,
	.dump = cmd_foreverypart_end_operation_dump,
	.execute = cmd_foreverypart_end_operation_execute
};

/*
 * Tag validation
 */

static bool cmd_foreverypart_validate_name_tag
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
    struct sieve_command *cmd)
{
	struct ext_foreverypart_loop *loop =
		(struct ext_foreverypart_loop *)cmd->data;
	struct sieve_ast_argument *tag = *arg;

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg, 1);

	/* Check syntax:
	 *   :name <string>
	 */
	if ( !sieve_validate_tag_parameter
		(valdtr, cmd, tag, *arg, NULL, 0, SAAT_STRING, TRUE) )
		return FALSE;
	loop->name = sieve_ast_argument_strc(*arg);

	/* Detach parameter */
	*arg = sieve_ast_arguments_detach(*arg, 1);
	return TRUE;
}

/*
 * Command registration
 */

static bool cmd_foreverypart_registered
(struct sieve_validator *valdtr, const struct sieve_extension *ext,
	struct sieve_command_registration *cmd_reg)
{
	sieve_validator_register_tag
		(valdtr, cmd_reg, ext, &foreverypart_name_tag, 0);
	return TRUE;
}

/*
 * Command validation
 */

static bool cmd_foreverypart_pre_validate
(struct sieve_validator *valdtr ATTR_UNUSED, struct sieve_command *cmd)
{
	struct ext_foreverypart_loop *loop;
	pool_t pool = sieve_command_pool(cmd);
	
	loop = p_new(pool, struct ext_foreverypart_loop, 1);
	cmd->data = loop;		

	return TRUE;
}

static bool cmd_foreverypart_validate
(struct sieve_validator *valdtr, struct sieve_command *cmd)
{
	struct sieve_ast_node *node = cmd->ast_node;
	unsigned int nesting = 0;

	/* Determine nesting depth of foreverypart commands at this point. */
	i_assert(node != NULL);
	node = sieve_ast_node_parent(node);
	while ( node != NULL && node->command != NULL ) {
		if ( sieve_command_is(node->command, cmd_foreverypart) )
			nesting++;
		node = sieve_ast_node_parent(node);
	}

	/* Enforce nesting limit
	   NOTE: this only recognizes the foreverypart command as a loop; if
	   new loop commands are introduced in the future, these must be 
	   recognized somehow. */
	if ( nesting + 1 > SIEVE_MAX_LOOP_DEPTH ) {
		sieve_command_validate_error(valdtr, cmd,
			"the nested foreverypart loop exceeds "
			"the nesting limit (<= %u levels)",
			SIEVE_MAX_LOOP_DEPTH);
		return FALSE;
	}

	return TRUE;
}

/*
 * Code generation
 */

static bool cmd_foreverypart_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd)
{
	struct ext_foreverypart_loop *loop =
		(struct ext_foreverypart_loop *)cmd->data;
	sieve_size_t block_begin, loop_jump;

	/* Emit FOREVERYPART_BEGIN operation */
	sieve_operation_emit(cgenv->sblock,
		cmd->ext, &foreverypart_begin_operation);

	/* Emit exit address */
	loop->exit_jumps = sieve_jumplist_create
		(sieve_command_pool(cmd), cgenv->sblock);
	sieve_jumplist_add(loop->exit_jumps,
		sieve_binary_emit_offset(cgenv->sblock, 0));
	block_begin = sieve_binary_block_get_size(cgenv->sblock);

	/* Generate loop block */
	if ( !sieve_generate_block(cgenv, cmd->ast_node) )
		return FALSE;

	/* Emit FOREVERYPART_END operation */
	sieve_operation_emit(cgenv->sblock,
		cmd->ext, &foreverypart_end_operation);
	loop_jump = sieve_binary_block_get_size(cgenv->sblock);
	i_assert(loop_jump > block_begin);
	(void)sieve_binary_emit_offset
		(cgenv->sblock, (loop_jump - block_begin));
	
	/* Resolve exit address */
	sieve_jumplist_resolve(loop->exit_jumps);

	return TRUE;
}

/*
 * Code dump
 */

static bool cmd_foreverypart_begin_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	unsigned int pc = *address;
	sieve_offset_t offset;

	sieve_code_dumpf(denv, "FOREVERYPART_BEGIN");
	sieve_code_descend(denv);

 	if ( !sieve_binary_read_offset(denv->sblock, address, &offset) )
		return FALSE;

	sieve_code_dumpf(denv, "END: %d [%08x]", offset, pc + offset);
	return TRUE;
}

static bool cmd_foreverypart_end_operation_dump
(const struct sieve_dumptime_env *denv,
	sieve_size_t *address ATTR_UNUSED)
{
	unsigned int pc = *address;
	sieve_offset_t offset;

	sieve_code_dumpf(denv, "FOREVERYPART_END");
	sieve_code_descend(denv);

	if ( !sieve_binary_read_offset(denv->sblock, address, &offset) )
		return FALSE;

	sieve_code_dumpf(denv, "BEGIN: -%d [%08x]", offset, pc - offset);
	return TRUE;
}

/*
 * Code execution
 */

static int cmd_foreverypart_begin_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	struct sieve_interpreter_loop *loop;
	struct ext_foreverypart_runtime_loop *fploop, *sfploop;
	unsigned int pc = *address;
	sieve_offset_t offset;
	sieve_size_t loop_end;
	pool_t pool;
	int ret;

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

	sieve_runtime_trace(renv, SIEVE_TRLVL_COMMANDS,
		"foreverypart loop begin");
	sieve_runtime_trace_descend(renv);

	sfploop = ext_foreverypart_runtime_loop_get_current(renv);

	if ( (ret=sieve_interpreter_loop_start(renv->interp,
		loop_end, &foreverypart_extension, &loop)) <= 0 )
		return ret;

	pool = sieve_interpreter_loop_get_pool(loop);
	fploop = p_new(pool, struct ext_foreverypart_runtime_loop, 1);
	
	if ( sfploop == NULL ) {
		if ( (ret=sieve_message_part_iter_init
			(&fploop->part_iter, renv)) <= 0 )
			return ret;
	} else {
		sieve_message_part_iter_children(&sfploop->part_iter,
			&fploop->part_iter);
	}
	fploop->part = sieve_message_part_iter_current(&fploop->part_iter);
	if (fploop->part != NULL) {
		sieve_interpreter_loop_set_context(loop, (void*)fploop);
	} else {
		/* No children parts to iterate */
		sieve_runtime_trace(renv, SIEVE_TRLVL_COMMANDS,
			"no children at this level");
		sieve_interpreter_loop_break(renv->interp, loop);
	} 
	return SIEVE_EXEC_OK;
}

static int cmd_foreverypart_end_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	struct sieve_interpreter_loop *loop;
	struct ext_foreverypart_runtime_loop *fploop;
	unsigned int pc = *address;
	sieve_offset_t offset;
	sieve_size_t loop_begin;

	/*
	 * Read operands
	 */

	if ( !sieve_binary_read_offset(renv->sblock, address, &offset) )
	{
		sieve_runtime_trace_error(renv, "invalid loop begin offset");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	loop_begin = pc - offset;

	/*
	 * Perform operation
	 */

	sieve_runtime_trace(renv,
		SIEVE_TRLVL_COMMANDS, "foreverypart loop end");
	sieve_runtime_trace_descend(renv);

	loop = sieve_interpreter_loop_get
		(renv->interp, *address, &foreverypart_extension);
	if ( loop == NULL ) {
		sieve_runtime_trace_error(renv, "no matching loop found");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	fploop = (struct ext_foreverypart_runtime_loop *)
		sieve_interpreter_loop_get_context(loop);
	i_assert(fploop->part != NULL);
	fploop->part = sieve_message_part_iter_next(&fploop->part_iter);
	if ( fploop->part == NULL ) {
		sieve_runtime_trace(renv,
			SIEVE_TRLVL_COMMANDS, "no more message parts");
		return sieve_interpreter_loop_break(renv->interp, loop);
	}

	sieve_runtime_trace(renv,
		SIEVE_TRLVL_COMMANDS, "switched to next message part");
	return sieve_interpreter_loop_next(renv->interp, loop, loop_begin);
}


