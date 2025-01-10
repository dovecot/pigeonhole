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
#include "testsuite-binary.h"
#include "testsuite-script.h"

/*
 * Commands
 */

static bool
cmd_test_binary_validate(struct sieve_validator *valdtr,
			struct sieve_command *cmd);
static bool
cmd_test_binary_generate(const struct sieve_codegen_env *cgenv,
			 struct sieve_command *ctx);

/* Test_binary_load command
 *
 * Syntax:
 *   test_binary_load <binary-name: string>
 */

const struct sieve_command_def cmd_test_binary_load = {
	.identifier = "test_binary_load",
	.type = SCT_COMMAND,
	.positional_args = 1,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = cmd_test_binary_validate,
	.generate = cmd_test_binary_generate,
};

/* Test_binary_save command
 *
 * Syntax:
 *   test_binary_save <binary-name: string>
 */

const struct sieve_command_def cmd_test_binary_save = {
	.identifier = "test_binary_save",
	.type = SCT_COMMAND,
	.positional_args = 1,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = cmd_test_binary_validate,
	.generate = cmd_test_binary_generate,
};

/*
 * Operations
 */

static bool
cmd_test_binary_operation_dump(const struct sieve_dumptime_env *denv,
			       sieve_size_t *address);
static int
cmd_test_binary_operation_execute(const struct sieve_runtime_env *renv,
				  sieve_size_t *address);

/* test_binary_create operation */

const struct sieve_operation_def test_binary_load_operation = {
	.mnemonic = "TEST_BINARY_LOAD",
	.ext_def = &testsuite_extension,
	.code = TESTSUITE_OPERATION_TEST_BINARY_LOAD,
	.dump = cmd_test_binary_operation_dump,
	.execute = cmd_test_binary_operation_execute,
};

/* test_binary_delete operation */

const struct sieve_operation_def test_binary_save_operation = {
	.mnemonic = "TEST_BINARY_SAVE",
	.ext_def = &testsuite_extension,
	.code = TESTSUITE_OPERATION_TEST_BINARY_SAVE,
	.dump = cmd_test_binary_operation_dump,
	.execute = cmd_test_binary_operation_execute,
};

/*
 * Validation
 */

static bool
cmd_test_binary_validate(struct sieve_validator *valdtr,
			 struct sieve_command *cmd)
{
	struct sieve_ast_argument *arg = cmd->first_positional;

	if (!sieve_validate_positional_argument(valdtr, cmd, arg, "binary-name",
						1, SAAT_STRING))
		return FALSE;

	return sieve_validator_argument_activate(valdtr, cmd, arg, FALSE);
}

/*
 * Code generation
 */

static bool
cmd_test_binary_generate(const struct sieve_codegen_env *cgenv,
			 struct sieve_command *cmd)
{
	/* Emit operation */
	if (sieve_command_is(cmd, cmd_test_binary_load)) {
		sieve_operation_emit(cgenv->sblock, cmd->ext,
				     &test_binary_load_operation);
	} else if (sieve_command_is(cmd, cmd_test_binary_save)) {
		sieve_operation_emit(cgenv->sblock, cmd->ext,
				     &test_binary_save_operation);
	} else {
		i_unreached();
	}

 	/* Generate arguments */
	if (!sieve_generate_arguments(cgenv, cmd, NULL))
		return FALSE;

	return TRUE;
}

/*
 * Code dump
 */

static bool
cmd_test_binary_operation_dump(const struct sieve_dumptime_env *denv,
			       sieve_size_t *address)
{
	sieve_code_dumpf(denv, "%s:", sieve_operation_mnemonic(denv->oprtn));

	sieve_code_descend(denv);

	return sieve_opr_string_dump(denv, address, "binary-name");
}

/*
 * Intepretation
 */

static int
cmd_test_binary_operation_execute(const struct sieve_runtime_env *renv,
				  sieve_size_t *address)
{
	const struct sieve_operation *oprtn = renv->oprtn;
	string_t *binary_name = NULL;
	int ret;

	/*
	 * Read operands
	 */

	/* Binary Name */

	ret = sieve_opr_string_read(renv, address, "binary-name", &binary_name);
	if (ret <= 0 )
		return ret;

	/*
	 * Perform operation
	 */

	if (sieve_operation_is(oprtn, test_binary_load_operation)) {
		struct sieve_binary *sbin =
			testsuite_binary_load(str_c(binary_name));

		if (sieve_runtime_trace_active(renv, SIEVE_TRLVL_COMMANDS)) {
			sieve_runtime_trace(renv, 0, "testsuite: "
					    "test_binary_load command");
			sieve_runtime_trace_descend(renv);
			sieve_runtime_trace(renv, 0, "load binary '%s'",
					    str_c(binary_name));
		}

		if ( sbin != NULL ) {
			testsuite_script_set_binary(renv, sbin);

			sieve_binary_unref(&sbin);
		} else {
			e_error(testsuite_sieve_instance->event,
				"failed to load binary %s", str_c(binary_name));
			return SIEVE_EXEC_FAILURE;
		}
	} else if ( sieve_operation_is(oprtn, test_binary_save_operation) ) {
		struct sieve_binary *sbin = testsuite_script_get_binary(renv);

		if ( sieve_runtime_trace_active(renv, SIEVE_TRLVL_COMMANDS) ) {
			sieve_runtime_trace(renv, 0, "testsuite: "
					    "test_binary_save command");
			sieve_runtime_trace_descend(renv);
			sieve_runtime_trace(renv, 0, "save binary '%s'",
					    str_c(binary_name));
		}

		if ( sbin != NULL )
			testsuite_binary_save(sbin, str_c(binary_name));
		else {
			e_error(testsuite_sieve_instance->event,
				"no compiled binary to save as %s",
				str_c(binary_name));
			return SIEVE_EXEC_FAILURE;
		}
	} else {
		i_unreached();
	}

	return SIEVE_EXEC_OK;
}
