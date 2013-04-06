/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-common.h"
#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-duplicate-common.h"

/* Duplicate test
 *
 * Syntax:
 *   Usage: "duplicate" [":seconds" <timeout: number>]
 *                      [":header" <header-name: string> /
 *                          ":value" <value: string>]
 *                      [":handle" <handle: string>]
 */

static bool tst_duplicate_registered
	(struct sieve_validator *valdtr, const struct sieve_extension *ext,
		struct sieve_command_registration *cmd_reg);
static bool tst_duplicate_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *ctx);

const struct sieve_command_def tst_duplicate = {
	"duplicate",
	SCT_TEST,
	0, 0, FALSE, FALSE,
	tst_duplicate_registered,
	NULL, NULL, NULL,
	tst_duplicate_generate,
	NULL,
};

/*
 * Duplicate test tags
 */

static bool tst_duplicate_validate_number_tag
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
		struct sieve_command *cmd);
static bool tst_duplicate_validate_string_tag
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
		struct sieve_command *cmd);

static const struct sieve_argument_def duplicate_seconds_tag = {
	"seconds",
	NULL,
	tst_duplicate_validate_number_tag,
	NULL, NULL, NULL,
};

static const struct sieve_argument_def duplicate_header_tag = {
	"header",
	NULL,
	tst_duplicate_validate_string_tag,
	NULL, NULL, NULL
};

static const struct sieve_argument_def duplicate_value_tag = {
	"value",
	NULL,
	tst_duplicate_validate_string_tag,
	NULL, NULL, NULL
};

static const struct sieve_argument_def duplicate_handle_tag = {
	"handle",
	NULL,
	tst_duplicate_validate_string_tag,
	NULL, NULL, NULL
};

/* Codes for optional arguments */

enum tst_duplicate_optional {
	OPT_END,
	OPT_SECONDS,
	OPT_HEADER,
	OPT_VALUE,
	OPT_HANDLE
};

/*
 * Duplicate operation
 */

static bool tst_duplicate_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_duplicate_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def tst_duplicate_operation = {
	"DUPLICATE", &duplicate_extension,
	0,
	tst_duplicate_operation_dump,
	tst_duplicate_operation_execute
};

/*
 * Tag validation
 */

static bool tst_duplicate_validate_number_tag
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
	struct sieve_command *cmd)
{
	const struct sieve_extension *ext = sieve_argument_ext(*arg);
	const struct ext_duplicate_config *config =
		(const struct ext_duplicate_config *) ext->context;
	struct sieve_ast_argument *tag = *arg;
	sieve_number_t seconds;

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);

	/* Check syntax:
	 *   :seconds number
	 */
	if ( !sieve_validate_tag_parameter
		(valdtr, cmd, tag, *arg, NULL, 0, SAAT_NUMBER, FALSE) ) {
		return FALSE;
	}

	seconds = sieve_ast_argument_number(*arg);
	/* Enforce :days <= max_period */
	if ( config->max_period > 0 && seconds > config->max_period ) {
		seconds = config->max_period;

		sieve_argument_validate_warning(valdtr, *arg,
			"specified :seconds value '%lu' is over the maximum",
			(unsigned long) seconds);
	}

	sieve_ast_argument_number_set(*arg, seconds);

	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);

	return TRUE;
}

static bool tst_duplicate_validate_string_tag
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
	struct sieve_command *cmd)
{
	struct sieve_ast_argument *tag = *arg;

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);

	/* Check syntax:
	 *   :header <header-name: string>
	 *   :value <value: string>
	 *   :handle <handle: string>
	 */
	if ( !sieve_validate_tag_parameter
		(valdtr, cmd, tag, *arg, NULL, 0, SAAT_STRING, FALSE) ) {
		return FALSE;
	}

	if ((bool)cmd->data == TRUE) {
		sieve_argument_validate_error(valdtr, *arg,
			"conflicting :header and :value arguments specified "
			"for the duplicate test");
		return TRUE;
	}

	if ( sieve_argument_is(tag, duplicate_header_tag) ) {
		if ( !sieve_command_verify_headers_argument(valdtr, *arg) )
			return FALSE;
		cmd->data = (void*)TRUE;
	} else if ( sieve_argument_is(tag, duplicate_value_tag) ) {
		cmd->data = (void*)TRUE;
	}

	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);
	return TRUE;
}

/*
 * Command registration
 */

static bool tst_duplicate_registered
(struct sieve_validator *valdtr, const struct sieve_extension *ext,
	struct sieve_command_registration *cmd_reg)
{
	sieve_validator_register_tag
		(valdtr, cmd_reg, ext, &duplicate_seconds_tag, OPT_SECONDS);
	sieve_validator_register_tag
		(valdtr, cmd_reg, ext, &duplicate_header_tag, OPT_HEADER);
	sieve_validator_register_tag
		(valdtr, cmd_reg, ext, &duplicate_value_tag, OPT_VALUE);
	sieve_validator_register_tag
		(valdtr, cmd_reg, ext, &duplicate_handle_tag, OPT_HANDLE);
	return TRUE;
}

/*
 * Code generation
 */

static bool tst_duplicate_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd)
{
	sieve_operation_emit(cgenv->sblock, cmd->ext, &tst_duplicate_operation);

	if ( !sieve_generate_arguments(cgenv, cmd, NULL) )
		return FALSE;

	return TRUE;
}

/*
 * Code dump
 */

static bool tst_duplicate_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	int opt_code = 0;

	sieve_code_dumpf(denv, "DUPLICATE");
	sieve_code_descend(denv);

	/* Dump optional operands */

	for (;;) {
		int opt;
		bool opok = TRUE;

		if ( (opt=sieve_opr_optional_dump(denv, address, &opt_code)) < 0 )
			return FALSE;

		if ( opt == 0 ) break;

		switch ( opt_code ) {
		case OPT_SECONDS:
			opok = sieve_opr_number_dump(denv, address, "seconds");
			break;
		case OPT_HEADER:
			opok = sieve_opr_string_dump(denv, address, "header");
			break;
		case OPT_VALUE:
			opok = sieve_opr_string_dump(denv, address, "value");
			break;
		case OPT_HANDLE:
			opok = sieve_opr_string_dump(denv, address, "handle");
			break;
		default:
			return FALSE;
		}

		if ( !opok ) return FALSE;
	}

	return TRUE;
}

/*
 * Code execution
 */

static int tst_duplicate_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address ATTR_UNUSED)
{
	const struct sieve_extension *ext = renv->oprtn->ext;
	const struct ext_duplicate_config *config =
		(const struct ext_duplicate_config *) ext->context;
	int opt_code = 0;
	string_t *handle = NULL, *header = NULL, *value = NULL;
	const char *val = NULL;
	size_t val_len = 0;
	sieve_number_t seconds = config->default_period;
	bool duplicate = FALSE;
	int ret;

	/*
	 * Read operands
	 */

	/* Optional operands */

	for (;;) {
		int opt;

		if ( (opt=sieve_opr_optional_read(renv, address, &opt_code)) < 0 )
			return SIEVE_EXEC_BIN_CORRUPT;

		if ( opt == 0 ) break;

		switch ( opt_code ) {
		case OPT_SECONDS:
			ret = sieve_opr_number_read(renv, address, "seconds", &seconds);
			break;
		case OPT_HEADER:
			ret = sieve_opr_string_read(renv, address, "header", &header);
			break;
		case OPT_VALUE:
			ret = sieve_opr_string_read(renv, address, "value", &value);
			break;
		case OPT_HANDLE:
			ret = sieve_opr_string_read(renv, address, "handle", &handle);
			break;
		default:
			sieve_runtime_trace_error(renv, "unknown optional operand");
			ret = SIEVE_EXEC_BIN_CORRUPT;
		}

		if ( ret <= 0 ) return ret;
	}

	/*
	 * Perform operation
	 */

	/* Trace */
	sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS, "duplicate test");
	sieve_runtime_trace_descend(renv);

	/* Get value */
	if (header != NULL) {
		if (mail_get_first_header(renv->msgdata->mail, str_c(header), &val) > 0)
			val_len = strlen(val);
	} else if (value != NULL) {
		val = str_c(value);
		val_len = str_len(value);
	} else if (renv->msgdata->id != NULL) {
		val = renv->msgdata->id;
		val_len = strlen(renv->msgdata->id);
	}

	/* Check duplicate */
	if (val == NULL) {
		duplicate = FALSE;
	} else {	
		if ((ret=ext_duplicate_check(renv, handle, val, val_len, seconds)) < 0)
			return SIEVE_EXEC_FAILURE;
		duplicate = ( ret > 0 );
	}

	/* Trace */
	if (duplicate) {
		sieve_runtime_trace(renv,	SIEVE_TRLVL_TESTS,
			"message is a duplicate");
	}	else {
		sieve_runtime_trace(renv,	SIEVE_TRLVL_TESTS,
			"message is not a duplicate");
	}

	/* Set test result for subsequent conditional jump */
	sieve_interpreter_set_test_result(renv->interp, duplicate);
	return SIEVE_EXEC_OK;
}

