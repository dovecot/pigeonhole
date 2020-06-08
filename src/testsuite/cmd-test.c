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
 * Test command
 *
 * Syntax:
 *   test <test-name: string> <block>
 */

static bool
cmd_test_validate(struct sieve_validator *valdtr, struct sieve_command *cmd);
static bool
cmd_test_generate(const struct sieve_codegen_env *cgenv,
		  struct sieve_command *md);

const struct sieve_command_def cmd_test = {
	.identifier = "test",
	.type = SCT_COMMAND,
	.positional_args = 1,
	.subtests = 0,
	.block_allowed = TRUE,
	.block_required = TRUE,
	.validate = cmd_test_validate,
	.generate = cmd_test_generate,
};

/*
 * Test operations
 */

/* Test operation */

static bool
cmd_test_operation_dump(const struct sieve_dumptime_env *denv,
			sieve_size_t *address);
static int
cmd_test_operation_execute(const struct sieve_runtime_env *renv,
			   sieve_size_t *address);

const struct sieve_operation_def test_operation = {
	.mnemonic = "TEST",
	.ext_def = &testsuite_extension,
	.code = TESTSUITE_OPERATION_TEST,
	.dump = cmd_test_operation_dump,
	.execute = cmd_test_operation_execute,
};

/* Test_finish operation */

static int
cmd_test_finish_operation_execute(const struct sieve_runtime_env *renv,
				  sieve_size_t *address);

const struct sieve_operation_def test_finish_operation = {
	.mnemonic = "TEST-FINISH",
	.ext_def = &testsuite_extension,
	.code = TESTSUITE_OPERATION_TEST_FINISH,
	.execute = cmd_test_finish_operation_execute,
};

/*
 * Validation
 */

static bool
cmd_test_validate(struct sieve_validator *valdtr ATTR_UNUSED,
		  struct sieve_command *cmd)
{
	struct sieve_ast_argument *arg = cmd->first_positional;

 	/* Check valid command placement */
	if (!sieve_command_is_toplevel(cmd)) {
		sieve_command_validate_error(
			valdtr, cmd, "tests cannot be nested: test "
			"command must be issued at top-level");
		return FALSE;
	}

	if (!sieve_validate_positional_argument(valdtr, cmd, arg, "test-name",
						1, SAAT_STRING))
		return FALSE;

	return sieve_validator_argument_activate(valdtr, cmd, arg, FALSE);
}

/*
 * Code generation
 */

static inline struct testsuite_generator_context *
_get_generator_context(struct sieve_generator *gentr)
{
	return (struct testsuite_generator_context *)
		sieve_generator_extension_get_context(gentr, testsuite_ext);
}

static bool
cmd_test_generate(const struct sieve_codegen_env *cgenv,
		  struct sieve_command *cmd)
{
	struct testsuite_generator_context *genctx =
		_get_generator_context(cgenv->gentr);

	sieve_operation_emit(cgenv->sblock, cmd->ext, &test_operation);

	/* Generate arguments */
	if (!sieve_generate_arguments(cgenv, cmd, NULL))
		return FALSE;

	/* Prepare jumplist */
	sieve_jumplist_reset(genctx->exit_jumps);
	sieve_jumplist_add(genctx->exit_jumps,
		sieve_binary_emit_offset(cgenv->sblock, 0));

	/* Test body */
	if (!sieve_generate_block(cgenv, cmd->ast_node))
		return FALSE;

	sieve_operation_emit(cgenv->sblock, cmd->ext, &test_finish_operation);

	/* Resolve exit jumps to this point */
	sieve_jumplist_resolve(genctx->exit_jumps);

	return TRUE;
}

/*
 * Code dump
 */

static bool
cmd_test_operation_dump(const struct sieve_dumptime_env *denv,
			sieve_size_t *address)
{
	sieve_size_t tst_begin;
	sieve_offset_t tst_end_offset;

	sieve_code_dumpf(denv, "TEST:");
	sieve_code_descend(denv);

	if (!sieve_opr_string_dump(denv, address, "test name"))
		return FALSE;

	sieve_code_mark(denv);
	tst_begin = *address;
	if (!sieve_binary_read_offset(denv->sblock, address, &tst_end_offset))
		return FALSE;
	sieve_code_dumpf(denv, "end: %d [%08llx]",
			 tst_end_offset,
			 (unsigned long long)tst_begin + tst_end_offset);

	return TRUE;
}

/*
 * Interpretation
 */

static int
cmd_test_operation_execute(const struct sieve_runtime_env *renv,
			   sieve_size_t *address)
{
	sieve_size_t tst_begin, tst_end;
	sieve_offset_t tst_end_offset;
	string_t *test_name;
	int ret;

	ret = sieve_opr_string_read(renv, address, "test name", &test_name);
	if (ret <= 0)
		return ret;

	tst_begin = *address;
	if (!sieve_binary_read_offset(renv->sblock, address, &tst_end_offset)) {
		sieve_runtime_trace_error(renv, "invalid end offset");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
	tst_end = tst_begin + tst_end_offset;

	sieve_runtime_trace_sep(renv);
	sieve_runtime_trace(renv, SIEVE_TRLVL_NONE,
			    "** Testsuite test start: \"%s\" (end: %08llx)",
			    str_c(test_name),
			    (unsigned long long)tst_end);

	return testsuite_test_start(renv, test_name, tst_end);
}

static int
cmd_test_finish_operation_execute(const struct sieve_runtime_env *renv,
				  sieve_size_t *address)
{
	sieve_runtime_trace(renv, SIEVE_TRLVL_NONE, "** Testsuite test end");
	sieve_runtime_trace_sep(renv);

	return testsuite_test_succeed(renv, address, NULL);
}
