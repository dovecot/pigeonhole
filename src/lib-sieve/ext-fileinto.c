/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

/* Extension fileinto
 * ------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5228
 * Implementation: full
 * Status: testing
 *
 */

#include "lib.h"
#include "str-sanitize.h"
#include "unichar.h"

#include "sieve-common.h"
#include "sieve-extensions.h"
#include "sieve-binary.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-message.h"
#include "sieve-actions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-result.h"

/*
 * Forward declarations
 */

static const struct sieve_command_def fileinto_command;
const struct sieve_operation_def fileinto_operation;
const struct sieve_extension_def fileinto_extension;

/*
 * Extension
 */

static bool
ext_fileinto_validator_load(const struct sieve_extension *ext,
			    struct sieve_validator *valdtr);

const struct sieve_extension_def fileinto_extension = {
	.name = "fileinto",
	.validator_load = ext_fileinto_validator_load,
	SIEVE_EXT_DEFINE_OPERATION(fileinto_operation),
};

static bool
ext_fileinto_validator_load(const struct sieve_extension *ext,
			    struct sieve_validator *valdtr)
{
	/* Register new command */
	sieve_validator_register_command(valdtr, ext, &fileinto_command);

	return TRUE;
}

/*
 * Fileinto command
 *
 * Syntax:
 *   fileinto <folder: string>
 */

static bool
cmd_fileinto_validate(struct sieve_validator *valdtr,
		      struct sieve_command *cmd);
static bool
cmd_fileinto_generate(const struct sieve_codegen_env *cgenv,
		      struct sieve_command *ctx);

static const struct sieve_command_def fileinto_command = {
	.identifier = "fileinto",
	.type = SCT_COMMAND,
	.positional_args = 1,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = cmd_fileinto_validate,
	.generate = cmd_fileinto_generate,
};

/*
 * Fileinto operation
 */

static bool
ext_fileinto_operation_dump(const struct sieve_dumptime_env *denv,
			    sieve_size_t *address);
static int
ext_fileinto_operation_execute(const struct sieve_runtime_env *renv,
			       sieve_size_t *address);

const struct sieve_operation_def fileinto_operation = {
	.mnemonic = "FILEINTO",
	.ext_def = &fileinto_extension,
	.dump = ext_fileinto_operation_dump,
	.execute = ext_fileinto_operation_execute,
};

/*
 * Validation
 */

static bool
cmd_fileinto_validate(struct sieve_validator *valdtr, struct sieve_command *cmd)
{
	struct sieve_ast_argument *arg = cmd->first_positional;

	if (!sieve_validate_positional_argument(valdtr, cmd, arg, "folder",
						1, SAAT_STRING))
		return FALSE;

	if (!sieve_validator_argument_activate(valdtr, cmd, arg, FALSE))
		return FALSE;

	/* Check name validity when folder argument is not a variable */
	if (sieve_argument_is_string_literal(arg)) {
		const char *folder = sieve_ast_argument_strc(arg), *error;

		if (!sieve_mailbox_check_name(folder, &error)) {
			sieve_command_validate_error(
				valdtr, cmd, "fileinto command: "
				"invalid folder name `%s' specified: %s",
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
cmd_fileinto_generate(const struct sieve_codegen_env *cgenv,
		      struct sieve_command *cmd)
{
	sieve_operation_emit(cgenv->sblock, cmd->ext, &fileinto_operation);

	/* Generate arguments */
	return sieve_generate_arguments(cgenv, cmd, NULL);
}

/*
 * Code dump
 */

static bool
ext_fileinto_operation_dump(const struct sieve_dumptime_env *denv,
			    sieve_size_t *address)
{
	sieve_code_dumpf(denv, "FILEINTO");
	sieve_code_descend(denv);

	if (sieve_action_opr_optional_dump(denv, address, NULL) != 0)
		return FALSE;

	return sieve_opr_string_dump(denv, address, "folder");
}

/*
 * Execution
 */

static int
ext_fileinto_operation_execute(const struct sieve_runtime_env *renv,
			       sieve_size_t *address)
{
	struct sieve_side_effects_list *slist = NULL;
	string_t *folder;
	const char *error;
	bool trace = sieve_runtime_trace_active(renv, SIEVE_TRLVL_ACTIONS);
	int ret = 0;

	/*
	 * Read operands
	 */

	/* Optional operands (side effects only) */
	if (sieve_action_opr_optional_read(renv, address, NULL,
					   &ret, &slist) != 0)
		return ret;

	/* Folder operand */
	ret = sieve_opr_string_read(renv, address, "folder", &folder);
	if (ret <= 0)
		return ret;

	/*
	 * Perform operation
	 */

	if (trace) {
		sieve_runtime_trace(renv, 0, "fileinto action");
		sieve_runtime_trace_descend(renv);
	}

	if (!sieve_mailbox_check_name(str_c(folder), &error)) {
		sieve_runtime_error(
			renv, NULL, "fileinto command: "
			"invalid folder name `%s' specified: %s",
			str_c(folder), error);
		return SIEVE_EXEC_FAILURE;
	}

	if (trace) {
		sieve_runtime_trace(renv, 0, "store message in mailbox `%s'",
				    str_sanitize(str_c(folder), 80));
	}

	/* Add action to result */
	if (sieve_act_store_add_to_result(renv, "fileinto", slist,
					  str_c(folder)) < 0)
		return SIEVE_EXEC_FAILURE;

	sieve_message_snapshot(renv->msgctx);
	return SIEVE_EXEC_OK;
}
