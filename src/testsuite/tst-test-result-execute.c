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

/*
 * Test_result_execute command
 *
 * Syntax:
 *   test_result_execute
 */

static bool tst_test_result_execute_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd);

const struct sieve_command_def tst_test_result_execute = {
	.identifier = "test_result_execute",
	.type = SCT_TEST,
	.positional_args = 0,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.generate = tst_test_result_execute_generate
};

/*
 * Operation
 */

static int tst_test_result_execute_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def test_result_execute_operation = {
	.mnemonic = "TEST_RESULT_EXECUTE",
	.ext_def = &testsuite_extension,
	.code = TESTSUITE_OPERATION_TEST_RESULT_EXECUTE,
	.execute = tst_test_result_execute_operation_execute
};

/*
 * Code generation
 */

static bool tst_test_result_execute_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *tst)
{
	sieve_operation_emit(cgenv->sblock, tst->ext, &test_result_execute_operation);

	return TRUE;
}

/*
 * Intepretation
 */

static int tst_test_result_execute_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address ATTR_UNUSED)
{
	bool result = TRUE;

	/*
	 * Perform operation
	 */

	sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS,
		"testsuite: test_result_execute test");

	result = testsuite_result_execute(renv);

	if ( sieve_runtime_trace_active(renv, SIEVE_TRLVL_TESTS) ) {
		sieve_runtime_trace_descend(renv);
		sieve_runtime_trace(renv, 0, "execution of result %s",
			( result ? "succeeded" : "failed" ));
	}

	/* Set result */
	sieve_interpreter_set_test_result(renv->interp, result);

	return SIEVE_EXEC_OK;
}




