/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str-sanitize.h"

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code.h"
#include "sieve-binary.h"
#include "sieve-dump.h"
#include "sieve-actions.h"

#include "testsuite-common.h"
#include "testsuite-mailstore.h"

/*
 * Commands
 */

static bool
cmd_test_mailbox_validate(struct sieve_validator *valdtr,
			  struct sieve_command *cmd);
static bool
cmd_test_mailbox_generate(const struct sieve_codegen_env *cgenv,
			  struct sieve_command *ctx);

/* Test_mailbox_create command
 *
 * Syntax:
 *   test_mailbox_create <mailbox: string>
 */

const struct sieve_command_def cmd_test_mailbox_create = {
	.identifier = "test_mailbox_create",
	.type = SCT_COMMAND,
	.positional_args = 1,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = cmd_test_mailbox_validate,
	.generate = cmd_test_mailbox_generate,
};

/* Test_mailbox_delete command
 *
 * Syntax:
 *   test_mailbox_create <mailbox: string>
 */

const struct sieve_command_def cmd_test_mailbox_delete = {
	.identifier = "test_mailbox_delete",
	.type = SCT_COMMAND,
	.positional_args = 1,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = cmd_test_mailbox_validate,
	.generate = cmd_test_mailbox_generate,
};

/*
 * Operations
 */

static bool
cmd_test_mailbox_operation_dump(const struct sieve_dumptime_env *denv,
				sieve_size_t *address);
static int
cmd_test_mailbox_operation_execute(const struct sieve_runtime_env *renv,
				   sieve_size_t *address);

/* Test_mailbox_create operation */

const struct sieve_operation_def test_mailbox_create_operation = {
	.mnemonic = "TEST_MAILBOX_CREATE",
	.ext_def = &testsuite_extension,
	.code = TESTSUITE_OPERATION_TEST_MAILBOX_CREATE,
	.dump = cmd_test_mailbox_operation_dump,
	.execute = cmd_test_mailbox_operation_execute,
};

/* Test_mailbox_delete operation */

const struct sieve_operation_def test_mailbox_delete_operation = {
	.mnemonic = "TEST_MAILBOX_DELETE",
	.ext_def = &testsuite_extension,
	.code = TESTSUITE_OPERATION_TEST_MAILBOX_DELETE,
	.dump = cmd_test_mailbox_operation_dump,
	.execute = cmd_test_mailbox_operation_execute,
};

/*
 * Validation
 */

static bool
cmd_test_mailbox_validate(struct sieve_validator *valdtr,
			  struct sieve_command *cmd)
{
	struct sieve_ast_argument *arg = cmd->first_positional;

	if (!sieve_validate_positional_argument(valdtr, cmd, arg, "mailbox", 1,
						SAAT_STRING))
		return FALSE;

	if ( !sieve_validator_argument_activate(valdtr, cmd, arg, FALSE) )
		return FALSE;

	/* Check name validity when folder argument is not a variable */
	if ( sieve_argument_is_string_literal(arg) ) {
		const char *folder = sieve_ast_argument_strc(arg), *error;

		if ( !sieve_mailbox_check_name(folder, &error) ) {
			sieve_command_validate_error(
				valdtr, cmd, "%s command: "
				"invalid mailbox `%s' specified: %s",
				sieve_command_identifier(cmd),
				str_sanitize(folder, 256), error);
			return FALSE;
		}
	}

	return TRUE;
}

/*
 * Code generation
 */

static bool
cmd_test_mailbox_generate(const struct sieve_codegen_env *cgenv,
			  struct sieve_command *cmd)
{
	/* Emit operation */
	if (sieve_command_is(cmd, cmd_test_mailbox_create)) {
		sieve_operation_emit(cgenv->sblock, cmd->ext,
				     &test_mailbox_create_operation);
	} else if (sieve_command_is(cmd, cmd_test_mailbox_delete)) {
		sieve_operation_emit(cgenv->sblock, cmd->ext,
				     &test_mailbox_delete_operation);
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
cmd_test_mailbox_operation_dump(const struct sieve_dumptime_env *denv,
				sieve_size_t *address)
{
	sieve_code_dumpf(denv, "%s:", sieve_operation_mnemonic(denv->oprtn));

	sieve_code_descend(denv);

	return sieve_opr_string_dump(denv, address, "mailbox");
}

/*
 * Intepretation
 */

static const char *
cmd_test_mailbox_get_command_name(const struct sieve_operation *oprtn)
{
	if (sieve_operation_is(oprtn, test_mailbox_create_operation))
		return "test_mailbox_create";
	if (sieve_operation_is(oprtn, test_mailbox_delete_operation))
		return "test_mailbox_delete";

	i_unreached();
}

static int
cmd_test_mailbox_create_execute(const struct sieve_runtime_env *renv,
				const char *mailbox)
{
	if (sieve_runtime_trace_active(renv, SIEVE_TRLVL_COMMANDS)) {
		sieve_runtime_trace(
			renv, 0,
			"testsuite/test_mailbox_create command");
		sieve_runtime_trace_descend(renv);
		sieve_runtime_trace(
			renv, 0, "create mailbox `%s'", mailbox);
	}

	testsuite_mailstore_mailbox_create(renv, mailbox);
	return SIEVE_EXEC_OK;
}

static int
cmd_test_mailbox_delete_execute(const struct sieve_runtime_env *renv,
				const char *mailbox)
{
	if (sieve_runtime_trace_active(renv, SIEVE_TRLVL_COMMANDS)) {
		sieve_runtime_trace(
			renv, 0,
			"testsuite/test_mailbox_delete command");
		sieve_runtime_trace_descend(renv);
		sieve_runtime_trace(
			renv, 0, "delete mailbox `%s'", mailbox);
	}

	/* FIXME: implement */
	return testsuite_test_failf(
		renv, "test_mailbox_delete: NOT IMPLEMENTED");
}

static int
cmd_test_mailbox_operation_execute(const struct sieve_runtime_env *renv,
				   sieve_size_t *address)
{
	const struct sieve_operation *oprtn = renv->oprtn;
	string_t *mailbox = NULL;
	const char *error;
	int ret;

	/*
	 * Read operands
	 */

	/* Mailbox */

	ret = sieve_opr_string_read(renv, address, "mailbox", &mailbox);
	if (ret <= 0)
		return ret;

	if (!sieve_mailbox_check_name(str_c(mailbox), &error)) {
		sieve_runtime_error(
			renv, NULL, "%s command: "
			"invalid mailbox `%s' specified: %s",
			cmd_test_mailbox_get_command_name(oprtn),
			str_c(mailbox), error);
		return SIEVE_EXEC_FAILURE;
	}

	/*
	 * Perform operation
	 */

	if (sieve_operation_is(oprtn, test_mailbox_create_operation))
		ret = cmd_test_mailbox_create_execute(renv, str_c(mailbox));
	else if (sieve_operation_is(oprtn, test_mailbox_delete_operation))
		ret = cmd_test_mailbox_delete_execute(renv, str_c(mailbox));
	else
		i_unreached();

	return ret;
}
