/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code.h"
#include "sieve-binary.h"
#include "sieve-dump.h"

#include "testsuite-common.h"

/*
 * Test_fail command
 *
 * Syntax:
 *   test_fail <reason: string>
 */

static bool
cmd_test_fail_validate(struct sieve_validator *valdtr,
		       struct sieve_command *cmd);
static bool
cmd_test_fail_generate(const struct sieve_codegen_env *cgenv,
		       struct sieve_command *ctx);

const struct sieve_command_def cmd_test_fail = {
	.identifier = "test_fail",
	.type = SCT_COMMAND,
	.positional_args = 1,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = cmd_test_fail_validate,
	.generate = cmd_test_fail_generate,
};

/*
 * Test operation
 */

static bool
cmd_test_fail_operation_dump(const struct sieve_dumptime_env *denv,
			     sieve_size_t *address);
static int
cmd_test_fail_operation_execute(const struct sieve_runtime_env *renv,
				sieve_size_t *address);

const struct sieve_operation_def test_fail_operation = {
	.mnemonic = "TEST_FAIL",
	.ext_def = &testsuite_extension,
	.code = TESTSUITE_OPERATION_TEST_FAIL,
	.dump = cmd_test_fail_operation_dump,
	.execute = cmd_test_fail_operation_execute,
};

/*
 * Validation
 */

static bool
cmd_test_fail_validate(struct sieve_validator *valdtr ATTR_UNUSED,
		       struct sieve_command *cmd)
{
	struct sieve_ast_argument *arg = cmd->first_positional;

	if (!sieve_validate_positional_argument(valdtr, cmd, arg, "reason", 1,
						SAAT_STRING))
		return FALSE;

	return sieve_validator_argument_activate(valdtr, cmd, arg, FALSE);
}

/*
 * Code generation
 */

static bool
cmd_test_fail_generate(const struct sieve_codegen_env *cgenv,
		       struct sieve_command *cmd)
{
	sieve_operation_emit(cgenv->sblock, cmd->ext, &test_fail_operation);

	/* Generate arguments */
	if (!sieve_generate_arguments(cgenv, cmd, NULL))
		return FALSE;

	return TRUE;
}

/*
 * Code dump
 */

static bool
cmd_test_fail_operation_dump(const struct sieve_dumptime_env *denv,
			     sieve_size_t *address)
{
	sieve_code_dumpf(denv, "TEST_FAIL:");
	sieve_code_descend(denv);

	if (!sieve_opr_string_dump(denv, address, "reason"))
		return FALSE;

	return TRUE;
}

/*
 * Intepretation
 */

static int
cmd_test_fail_operation_execute(const struct sieve_runtime_env *renv,
				sieve_size_t *address)
{
	string_t *reason;
	int ret;

	ret = sieve_opr_string_read(renv, address, "reason", &reason);
	if (ret <= 0)
		return ret;

	sieve_runtime_trace(renv, SIEVE_TRLVL_COMMANDS, "testsuite: "
			    "test_fail command; FAIL current test");

	return testsuite_test_fail(renv, reason);
}
