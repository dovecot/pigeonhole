/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str-sanitize.h"
#include "mail-storage.h"

#include "rfc2822.h"
#include "edit-mail.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-message.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-editheader-common.h"

/*
 * Addheader command
 *
 * Syntax
 *   "addheader" [":last"] <field-name: string> <value: string>
 */

static bool
cmd_addheader_registered(struct sieve_validator *valdtr,
			 const struct sieve_extension *ext,
			 struct sieve_command_registration *cmd_reg);
static bool
cmd_addheader_validate(struct sieve_validator *valdtr,
		       struct sieve_command *tst);
static bool
cmd_addheader_generate(const struct sieve_codegen_env *cgenv,
		       struct sieve_command *ctx);

const struct sieve_command_def addheader_command = {
	.identifier = "addheader",
	.type = SCT_COMMAND,
	.positional_args = 2,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.registered = cmd_addheader_registered,
	.validate = cmd_addheader_validate,
	.generate = cmd_addheader_generate
};

/*
 * Addheader command tags
 */

/* Argument objects */

static const struct sieve_argument_def addheader_last_tag = {
	.identifier = "last",
};

/* Codes for optional arguments */

enum cmd_addheader_optional {
	OPT_END,
	OPT_LAST,
};

/*
 * Addheader operation
 */

static bool
cmd_addheader_operation_dump(const struct sieve_dumptime_env *denv,
			     sieve_size_t *address);
static int
cmd_addheader_operation_execute(const struct sieve_runtime_env *renv,
				sieve_size_t *address);

const struct sieve_operation_def addheader_operation = {
	.mnemonic = "ADDHEADER",
	.ext_def = &editheader_extension,
	.code = EXT_EDITHEADER_OPERATION_ADDHEADER,
	.dump = cmd_addheader_operation_dump,
	.execute = cmd_addheader_operation_execute,
};

/*
 * Utility
 */

static bool _str_contains_nul(const string_t *str)
{
	const unsigned char *p, *pend;

	p = str_data(str);
	pend = p + str_len(str);
	while (p < pend) {
		if (*p == '\0')
			return TRUE;
		p++;
	}
	return FALSE;
}

/*
 * Validation
 */

static bool
cmd_addheader_validate(struct sieve_validator *valdtr,
		       struct sieve_command *cmd)
{
	struct sieve_ast_argument *arg = cmd->first_positional;

	/* Check field-name syntax */

	if (!sieve_validate_positional_argument(valdtr, cmd, arg, "field-name",
						1, SAAT_STRING))
		return FALSE;
	if (!sieve_validator_argument_activate(valdtr, cmd, arg, FALSE))
		return FALSE;

	if (sieve_argument_is_string_literal(arg)) {
		string_t *fname = sieve_ast_argument_str(arg);

		if (!rfc2822_header_field_name_verify(str_c(fname),
						      str_len(fname))) {
			sieve_argument_validate_error(
				valdtr, arg, "addheader command: "
				"specified field name '%s' is invalid",
				str_sanitize(str_c(fname), 80));
			return FALSE;
		}

		if (!ext_editheader_header_allow_add(cmd->ext, str_c(fname))) {
			sieve_argument_validate_warning(
				valdtr, arg, "addheader command: "
				"adding specified header field '%s' is forbidden; "
				"modification will be denied",
				str_sanitize(str_c(fname), 80));
		}
	}

	/* Check value syntax */

	arg = sieve_ast_argument_next(arg);

	if (!sieve_validate_positional_argument(valdtr, cmd, arg, "value", 2,
						SAAT_STRING))
		return FALSE;
	if (!sieve_validator_argument_activate(valdtr, cmd, arg, FALSE))
		return FALSE;

	if (sieve_argument_is_string_literal(arg)) {
		string_t *fvalue = sieve_ast_argument_str(arg);

		if (_str_contains_nul(fvalue)) {
			sieve_argument_validate_error(
				valdtr, arg,
				"addheader command: specified value '%s' is invalid "
				"(contains NUL character)",
				str_sanitize(str_c(fvalue), 80));
			return FALSE;
		}

		if (!rfc2822_header_field_body_verify(str_c(
			fvalue), str_len(fvalue), TRUE, TRUE)) {
			sieve_argument_validate_error(
				valdtr, arg,
				"addheader command: specified value '%s' is invalid",
				str_sanitize(str_c(fvalue), 80));
			return SIEVE_EXEC_FAILURE;
		}

		if (ext_editheader_header_too_large(
			cmd->ext, str_len(fvalue))) {
			sieve_argument_validate_error(
				valdtr, arg, "addheader command: "
				"specified header value '%s' is too large (%zu bytes)",
				str_sanitize(str_c(fvalue), 80),
				str_len(fvalue));
			return SIEVE_EXEC_FAILURE;
		}
	}
	return TRUE;
}

/*
 * Command registration
 */

static bool
cmd_addheader_registered(struct sieve_validator *valdtr,
			 const struct sieve_extension *ext,
			 struct sieve_command_registration *cmd_reg)
{
	sieve_validator_register_tag(valdtr, cmd_reg, ext,
				     &addheader_last_tag, OPT_LAST);
	return TRUE;
}

/*
 * Code generation
 */

static bool
cmd_addheader_generate(const struct sieve_codegen_env *cgenv,
		       struct sieve_command *cmd)
{
	(void)sieve_operation_emit(cgenv->sblock, cmd->ext,
				   &addheader_operation);

	/* Generate arguments */
	return sieve_generate_arguments(cgenv, cmd, NULL);
}

/*
 * Code dump
 */

static bool
cmd_addheader_operation_dump(const struct sieve_dumptime_env *denv,
			     sieve_size_t *address)
{
	int opt_code = 0;

	sieve_code_dumpf(denv, "addheader");
	sieve_code_descend(denv);

	/* Dump optional operands */

	for (;;) {
		int opt;

		opt = sieve_opr_optional_dump(denv, address, &opt_code);
		if (opt < 0)
			return FALSE;
		if (opt == 0)
			break;

		if (opt_code == OPT_LAST)
			sieve_code_dumpf(denv, "last");
		else
			return FALSE;
	}

	return (sieve_opr_string_dump(denv, address, "field-name") &&
		sieve_opr_string_dump(denv, address, "value"));
}

/*
 * Interpretation
 */

static int
cmd_addheader_operation_execute(const struct sieve_runtime_env *renv,
				sieve_size_t *address)
{
	const struct sieve_extension *this_ext = renv->oprtn->ext;
	string_t *field_name;
	string_t *value;
	struct edit_mail *edmail;
	bool last = FALSE;
	int opt_code = 0;
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
		case OPT_LAST:
			last = TRUE;
			break;
		default:
			sieve_runtime_trace_error(
				renv, "unknown optional operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}
	}

	/* Read message */

	ret = sieve_opr_string_read(renv, address, "field-name", &field_name);
	if (ret <= 0)
		return ret;
	ret = sieve_opr_string_read(renv, address, "value", &value);
	if (ret <= 0)
		return ret;

	/*
	 * Verify arguments
	 */

	if (!rfc2822_header_field_name_verify(str_c(field_name),
					      str_len(field_name))) {
		sieve_runtime_error(
			renv, NULL, "addheader action: "
			"specified field name '%s' is invalid",
			str_sanitize(str_c(field_name), 80));
		return SIEVE_EXEC_FAILURE;
	}

	if (!ext_editheader_header_allow_add(this_ext, str_c(field_name))) {
		sieve_runtime_warning(
			renv, NULL, "addheader action: "
			"adding specified header field '%s' is forbidden; "
			"modification denied",
			str_sanitize(str_c(field_name), 80));
		return SIEVE_EXEC_OK;
	}

	if (_str_contains_nul(value)) {
		sieve_runtime_error(
			renv, NULL, "addheader action: "
			"specified value '%s' is invalid (contains NUL character)",
			str_sanitize(str_c(value), 80));
		return SIEVE_EXEC_FAILURE;
	}

	if (ext_editheader_header_too_large(this_ext, str_len(value))) {
		sieve_runtime_error(
			renv, NULL, "addheader action: "
			"specified header value '%s' is too large (%zu bytes)",
			str_sanitize(str_c(value), 80), str_len(value));
		return SIEVE_EXEC_FAILURE;
	}

	/*
	 * Perform operation
	 */

	sieve_runtime_trace(
		renv, SIEVE_TRLVL_COMMANDS, "addheader \"%s: %s\"",
		str_sanitize(str_c(field_name), 80),
		str_sanitize(str_c(value), 80));

	edmail = sieve_message_edit(renv->msgctx);
	edit_mail_header_add(
		edmail, rfc2822_header_field_name_sanitize(str_c(field_name)),
		str_c(value), last);
	return SIEVE_EXEC_OK;
}
