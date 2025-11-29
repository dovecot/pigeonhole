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
cmd_test_imap_metadata_registered(struct sieve_validator *valdtr,
				  const struct sieve_extension *ext,
				  struct sieve_command_registration *cmd_reg);
static bool
cmd_test_imap_metadata_validate(struct sieve_validator *valdtr,
				struct sieve_command *cmd);
static bool
cmd_test_imap_metadata_generate(const struct sieve_codegen_env *cgenv,
				struct sieve_command *ctx);

/* Test_mailbox_create command

   Syntax:
     test_imap_metadata_set
       <mailbox: string> <annotation: string> <value:string>
 */

const struct sieve_command_def cmd_test_imap_metadata_set = {
	.identifier = "test_imap_metadata_set",
	.type = SCT_COMMAND,
	.positional_args = 2,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.registered = cmd_test_imap_metadata_registered,
	.validate = cmd_test_imap_metadata_validate,
	.generate = cmd_test_imap_metadata_generate,
};

/*
 * Command tags
 */

static bool
cmd_test_imap_metadata_validate_mailbox_tag(struct sieve_validator *valdtr,
					    struct sieve_ast_argument **arg,
					    struct sieve_command *cmd);

static const struct sieve_argument_def test_imap_metadata_mailbox_tag = {
	.identifier = "mailbox",
	.validate = cmd_test_imap_metadata_validate_mailbox_tag
};

/*
 * Operations
 */

static bool
cmd_test_imap_metadata_operation_dump(const struct sieve_dumptime_env *denv,
				      sieve_size_t *address);
static int
cmd_test_imap_metadata_operation_execute(const struct sieve_runtime_env *renv,
					 sieve_size_t *address);

/* Test_mailbox_create operation */

const struct sieve_operation_def test_imap_metadata_set_operation = {
	.mnemonic = "TEST_IMAP_METADATA_SET",
	.ext_def = &testsuite_extension,
	.code = TESTSUITE_OPERATION_TEST_IMAP_METADATA_SET,
	.dump = cmd_test_imap_metadata_operation_dump,
	.execute = cmd_test_imap_metadata_operation_execute,
};

/* Codes for optional arguments */

enum cmd_vacation_optional {
	OPT_END,
	OPT_MAILBOX,
};

/*
 * Tag validation
 */

static bool
cmd_test_imap_metadata_validate_mailbox_tag(struct sieve_validator *valdtr,
					    struct sieve_ast_argument **arg,
					    struct sieve_command *cmd)
{
	struct sieve_ast_argument *tag = *arg;

	/* Delete this tag */
	*arg = sieve_ast_arguments_detach(*arg, 1);

	/* Check syntax:
	     :mailbox string
	 */
	if (!sieve_validate_tag_parameter(valdtr, cmd, tag, *arg, NULL,
					  0, SAAT_STRING, FALSE))
		return FALSE;

	/* Check name validity when mailbox argument is not a variable */
	if (sieve_argument_is_string_literal(*arg)) {
		const char *mailbox = sieve_ast_argument_strc(*arg), *error;

		if (!sieve_mailbox_check_name(mailbox, &error)) {
			sieve_command_validate_error(
				valdtr, cmd, "test_imap_metadata_set command: "
				"invalid mailbox name '%s' specified: %s",
				str_sanitize(mailbox, 256), error);
			return FALSE;
		}
	}

	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);
	return TRUE;
}

/*
 * Command registration
 */

static bool
cmd_test_imap_metadata_registered(struct sieve_validator *valdtr,
				  const struct sieve_extension *ext,
				  struct sieve_command_registration *cmd_reg)
{
	sieve_validator_register_tag(valdtr, cmd_reg, ext,
				     &test_imap_metadata_mailbox_tag,
				     OPT_MAILBOX);
	return TRUE;
}


/*
 * Validation
 */

static bool
cmd_test_imap_metadata_validate(struct sieve_validator *valdtr,
				struct sieve_command *cmd)
{
	struct sieve_ast_argument *arg = cmd->first_positional;

	if (!sieve_validate_positional_argument(valdtr, cmd, arg, "annotation",
						2, SAAT_STRING))
		return FALSE;
	if (!sieve_validator_argument_activate(valdtr, cmd, arg, FALSE))
		return FALSE;

	arg = sieve_ast_argument_next(arg);

	if (!sieve_validate_positional_argument(valdtr, cmd, arg, "value",
						3, SAAT_STRING))
		return FALSE;
	if (!sieve_validator_argument_activate(valdtr, cmd, arg, FALSE))
		return FALSE;
	return TRUE;
}

/*
 * Code generation
 */

static bool
cmd_test_imap_metadata_generate(const struct sieve_codegen_env *cgenv,
				struct sieve_command *cmd)
{
	/* Emit operation */
	if (sieve_command_is(cmd, cmd_test_imap_metadata_set)) {
		sieve_operation_emit(cgenv->sblock, cmd->ext,
				     &test_imap_metadata_set_operation);
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
cmd_test_imap_metadata_operation_dump(const struct sieve_dumptime_env *denv,
				      sieve_size_t *address)
{
	int opt_code = 0;

	sieve_code_dumpf(denv, "%s:", sieve_operation_mnemonic(denv->oprtn));
	sieve_code_descend(denv);

	/* Dump optional operands */

	for (;;) {
		int opt;
		bool opok = TRUE;

		opt = sieve_opr_optional_dump(denv, address, &opt_code);
		if (opt < 0)
			return FALSE;
		if (opt == 0)
			break;

		switch (opt_code) {
		case OPT_MAILBOX:
			opok = sieve_opr_string_dump(denv, address, "mailbox");
			break;
		default:
			return FALSE;
		}

		if (!opok)
			return FALSE;
	}

	return (sieve_opr_string_dump(denv, address, "annotation") &&
		sieve_opr_string_dump(denv, address, "value"));
}

/*
 * Intepretation
 */

static int
cmd_test_imap_metadata_operation_execute(const struct sieve_runtime_env *renv,
					 sieve_size_t *address)
{
	const struct sieve_operation *oprtn = renv->oprtn;
	int opt_code = 0;
	string_t *mailbox = NULL, *annotation = NULL, *value = NULL;
	const char *error;
	int ret;

	/*
	 * Read operands
	 */

	/* Optional operands */

	for (;;) {
		int opt;

		opt = sieve_opr_optional_read(renv, address, &opt_code);
		if (opt < 0)
			return SIEVE_EXEC_BIN_CORRUPT;
		if (opt == 0)
			break;

		switch (opt_code) {
		case OPT_MAILBOX:
			ret = sieve_opr_string_read(renv, address, "mailbox",
						    &mailbox);
			if (ret > 0 &&
			    !sieve_mailbox_check_name(str_c(mailbox), &error)) {
				sieve_runtime_error(
					renv, NULL,
					"test_imap_metadata_set command: "
					"invalid mailbox name '%s' specified: %s",
					str_c(mailbox), error);
				ret = SIEVE_EXEC_FAILURE;
			}
			break;
		default:
			sieve_runtime_trace_error(
				renv, "unknown optional operand");
			ret = SIEVE_EXEC_BIN_CORRUPT;
		}

		if (ret <= 0)
			return ret;
	}

	/* Fixed operands */

	ret = sieve_opr_string_read(renv, address, "annotation", &annotation);
	if (ret <= 0)
		return ret;
	ret = sieve_opr_string_read(renv, address, "value", &value);
	if (ret <= 0)
		return ret;

	/*
	 * Perform operation
	 */

	if (sieve_operation_is(oprtn, test_imap_metadata_set_operation)) {
		if (sieve_runtime_trace_active(renv, SIEVE_TRLVL_COMMANDS)) {
			sieve_runtime_trace(renv, 0,
				"testsuite/test_imap_metadata_set command");
			sieve_runtime_trace_descend(renv);
			if (mailbox == NULL) {
				sieve_runtime_trace(renv, 0,
					"set server annotation '%s'",
					str_c(annotation));
			} else {
				sieve_runtime_trace(renv, 0,
					"set annotation '%s' for mailbox '%s'",
					str_c(annotation), str_c(mailbox));
			}
		}

		if (testsuite_mailstore_set_imap_metadata(
			(mailbox == NULL ? NULL : str_c(mailbox)),
			str_c(annotation), str_c(value)) < 0)
			return SIEVE_EXEC_FAILURE;
	}
	return SIEVE_EXEC_OK;
}
