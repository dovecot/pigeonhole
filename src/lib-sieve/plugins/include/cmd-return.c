/* Copyright (c) 2002-2016 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-code.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-include-common.h"

/*
 * Return command
 *
 * Syntax
 *   return
 */

static bool cmd_return_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd);

const struct sieve_command_def cmd_return = {
	.identifier = "return",
	.type = SCT_COMMAND,
	.positional_args = 0,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.generate = cmd_return_generate
};

/*
 * Return operation
 */

static int opc_return_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def return_operation = {
	.mnemonic = "RETURN",
	.ext_def = &include_extension,
	.code = EXT_INCLUDE_OPERATION_RETURN,
	.execute = opc_return_execute
};

/*
 * Code generation
 */

static bool cmd_return_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd)
{
	sieve_operation_emit(cgenv->sblock, cmd->ext, &return_operation);

	return TRUE;
}

/*
 * Execution
 */

static int opc_return_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address ATTR_UNUSED)
{
	ext_include_execute_return(renv);
	return SIEVE_EXEC_OK;
}


