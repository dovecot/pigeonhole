/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-script.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code.h"
#include "sieve-binary.h"
#include "sieve-dump.h"
#include "sieve.h"

#include "testsuite-common.h"
#include "testsuite-result.h"
#include "testsuite-message.h"
#include "testsuite-smtp.h"

/*
 * Commands
 */

static bool
cmd_test_result_generate(const struct sieve_codegen_env *cgenv,
			 struct sieve_command *cmd);

/* Test_result_reset command
 *
 * Syntax:
 *   test_result_reset
 */

const struct sieve_command_def cmd_test_result_reset = {
	.identifier = "test_result_reset",
	.type = SCT_COMMAND,
	.positional_args = 0,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.generate = cmd_test_result_generate,
};

/* Test_result_print command
 *
 * Syntax:
 *   test_result_print
 */

const struct sieve_command_def cmd_test_result_print = {
	.identifier = "test_result_print",
	.type = SCT_COMMAND,
	.positional_args = 0,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.generate = cmd_test_result_generate,
};

/*
 * Operations
 */

/* test_result_reset */

static int
cmd_test_result_reset_operation_execute(const struct sieve_runtime_env *renv,
					sieve_size_t *address);

const struct sieve_operation_def test_result_reset_operation = {
	.mnemonic = "TEST_RESULT_RESET",
	.ext_def = &testsuite_extension,
	.code = TESTSUITE_OPERATION_TEST_RESULT_RESET,
	.execute = cmd_test_result_reset_operation_execute,
};

/* test_result_print */

static int
cmd_test_result_print_operation_execute(const struct sieve_runtime_env *renv,
					sieve_size_t *address);

const struct sieve_operation_def test_result_print_operation = {
	.mnemonic = "TEST_RESULT_PRINT",
	.ext_def = &testsuite_extension,
	.code = TESTSUITE_OPERATION_TEST_RESULT_PRINT,
	.execute = cmd_test_result_print_operation_execute,
};

/*
 * Code generation
 */

static bool
cmd_test_result_generate(const struct sieve_codegen_env *cgenv,
			 struct sieve_command *cmd)
{
	if (sieve_command_is(cmd, cmd_test_result_reset)) {
		sieve_operation_emit(cgenv->sblock, cmd->ext,
				     &test_result_reset_operation);
	} else if (sieve_command_is(cmd, cmd_test_result_print)) {
		sieve_operation_emit(cgenv->sblock, cmd->ext,
				     &test_result_print_operation);
	} else {
		i_unreached();
	}

	return TRUE;
}

/*
 * Intepretation
 */

static int
cmd_test_result_reset_operation_execute(const struct sieve_runtime_env *renv,
					sieve_size_t *address ATTR_UNUSED)
{
	sieve_runtime_trace(renv, SIEVE_TRLVL_COMMANDS,	"testsuite: "
			    "test_result_reset command; reset script result");

	testsuite_result_reset(renv);
	testsuite_smtp_reset();

	return SIEVE_EXEC_OK;
}

static int
cmd_test_result_print_operation_execute(const struct sieve_runtime_env *renv,
					sieve_size_t *address ATTR_UNUSED)
{
	sieve_runtime_trace(renv, SIEVE_TRLVL_COMMANDS,	"testsuite: "
			    "test_result_print command; print script result ");

	testsuite_result_print(renv);

	return SIEVE_EXEC_OK;
}
