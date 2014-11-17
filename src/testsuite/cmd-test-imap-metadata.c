/* Copyright (c) 2002-2014 Pigeonhole authors, see the included COPYING file
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
#include "testsuite-mailstore.h"

/*
 * Commands
 */

static bool cmd_test_imap_metadata_validate
	(struct sieve_validator *valdtr, struct sieve_command *cmd);
static bool cmd_test_imap_metadata_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *ctx);

/* Test_mailbox_create command
 *
 * Syntax:
 *   test_imap_metadata_set
 *     <mailbox: string> <annotation: string> <value:string>
 */

const struct sieve_command_def cmd_test_imap_metadata_set = {
	"test_imap_metadata_set",
	SCT_COMMAND,
	3, 0, FALSE, FALSE,
	NULL, NULL,
	cmd_test_imap_metadata_validate,
	NULL,
	cmd_test_imap_metadata_generate,
	NULL
};

/*
 * Operations
 */

static bool cmd_test_imap_metadata_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int cmd_test_imap_metadata_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

/* Test_mailbox_create operation */

const struct sieve_operation_def test_imap_metadata_set_operation = {
	"TEST_IMAP_METADATA_SET",
	&testsuite_extension,
	TESTSUITE_OPERATION_TEST_IMAP_METADATA_SET,
	cmd_test_imap_metadata_operation_dump,
	cmd_test_imap_metadata_operation_execute
};

/*
 * Validation
 */

static bool cmd_test_imap_metadata_validate
(struct sieve_validator *valdtr, struct sieve_command *cmd)
{
	struct sieve_ast_argument *arg = cmd->first_positional;

	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "mailbox", 1, SAAT_STRING) )
		return FALSE;

	if (!sieve_validator_argument_activate(valdtr, cmd, arg, FALSE))
		return FALSE;

	arg = sieve_ast_argument_next(arg);

	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "annotation", 2, SAAT_STRING) )
		return FALSE;

	if (!sieve_validator_argument_activate(valdtr, cmd, arg, FALSE))
		return FALSE;

	arg = sieve_ast_argument_next(arg);

	if ( !sieve_validate_positional_argument
		(valdtr, cmd, arg, "value", 3, SAAT_STRING) )
		return FALSE;

	if (!sieve_validator_argument_activate(valdtr, cmd, arg, FALSE))
		return FALSE;
	return TRUE;
}

/*
 * Code generation
 */

static bool cmd_test_imap_metadata_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd)
{
	/* Emit operation */
	if ( sieve_command_is(cmd, cmd_test_imap_metadata_set) )
		sieve_operation_emit
			(cgenv->sblock, cmd->ext, &test_imap_metadata_set_operation);
	else
		i_unreached();

 	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, cmd, NULL) )
		return FALSE;
	return TRUE;
}

/*
 * Code dump
 */

static bool cmd_test_imap_metadata_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	sieve_code_dumpf(denv, "%s:", sieve_operation_mnemonic(denv->oprtn));

	sieve_code_descend(denv);

	return (sieve_opr_string_dump(denv, address, "mailbox") &&
		sieve_opr_string_dump(denv, address, "annotation") &&
		sieve_opr_string_dump(denv, address, "value"));
}

/*
 * Intepretation
 */

static int cmd_test_imap_metadata_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	const struct sieve_operation *oprtn = renv->oprtn;
	string_t *mailbox = NULL, *annotation = NULL, *value = NULL;
	int ret;

	/*
	 * Read operands
	 */

	if ( (ret=sieve_opr_string_read
		(renv, address, "mailbox", &mailbox)) <= 0 )
		return ret;
	if ( (ret=sieve_opr_string_read
		(renv, address, "annotation", &annotation)) <= 0 )
		return ret;
	if ( (ret=sieve_opr_string_read
		(renv, address, "value", &value)) <= 0 )
		return ret;

	/*
	 * Perform operation
	 */

	if ( sieve_operation_is(oprtn, test_imap_metadata_set_operation) ) {
		if ( sieve_runtime_trace_active(renv, SIEVE_TRLVL_COMMANDS) ) {
			sieve_runtime_trace(renv, 0, "testsuite/test_imap_metadata_set command");
			sieve_runtime_trace_descend(renv);
			sieve_runtime_trace(renv, 0, "set annotation `%s'", str_c(mailbox));
		}

		if (testsuite_mailstore_set_imap_metadata
			(str_c(mailbox), str_c(annotation), str_c(value)) < 0)
			return SIEVE_EXEC_FAILURE;
	}

	return SIEVE_EXEC_OK;
}
