/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

/*
 * Stop command
 *
 * Syntax
 *   stop
 */

static bool cmd_stop_generate
	(const struct sieve_codegen_env *cgenv,
		struct sieve_command *ctx ATTR_UNUSED);
static bool cmd_stop_validate
	(struct sieve_validator *valdtr, struct sieve_command *cmd);

const struct sieve_command_def cmd_stop = {
	.identifier = "stop",
	.type = SCT_COMMAND,
	.positional_args = 0,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = cmd_stop_validate,
	.generate = cmd_stop_generate
};

/*
 * Stop operation
 */

static int opc_stop_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def cmd_stop_operation = {
	.mnemonic = "STOP",
	.code = SIEVE_OPERATION_STOP,
	.execute = opc_stop_execute
};

/*
 * Command validation
 */

static bool cmd_stop_validate
(struct sieve_validator *valdtr ATTR_UNUSED, struct sieve_command *cmd)
{
	sieve_command_exit_block_unconditionally(cmd);

	return TRUE;
}

/*
 * Code generation
 */

static bool cmd_stop_generate
(const struct sieve_codegen_env *cgenv,
	struct sieve_command *cmd ATTR_UNUSED)
{
	sieve_operation_emit(cgenv->sblock, NULL, &cmd_stop_operation);

	return TRUE;
}

/*
 * Code execution
 */

static int opc_stop_execute
(const struct sieve_runtime_env *renv,  sieve_size_t *address ATTR_UNUSED)
{
	sieve_runtime_trace(renv, SIEVE_TRLVL_COMMANDS,
		"stop command; end all script execution");

	sieve_interpreter_interrupt(renv->interp);

	return SIEVE_EXEC_OK;
}

