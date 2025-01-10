/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str-sanitize.h"

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-address-parts.h"
#include "sieve-message.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-match.h"

#include "ext-date-common.h"

#include <time.h>

/*
 * Tests
 */

static bool
tst_date_validate(struct sieve_validator *valdtr, struct sieve_command *tst);
static bool
tst_date_generate(const struct sieve_codegen_env *cgenv,
		  struct sieve_command *ctx);

/* Date test
 *
 * Syntax:
 *    date [<":zone" <time-zone: string>> / ":originalzone"]
 *         [COMPARATOR] [MATCH-TYPE] <header-name: string>
 *         <date-part: string> <key-list: string-list>
 */

static bool
tst_date_registered(struct sieve_validator *valdtr,
		    const struct sieve_extension *ext,
		    struct sieve_command_registration *cmd_reg);

const struct sieve_command_def date_test = {
	.identifier = "date",
	.type = SCT_TEST,
	.positional_args = 3,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.registered = tst_date_registered,
	.validate = tst_date_validate,
	.generate = tst_date_generate,
};

/* Currentdate test
 *
 * Syntax:
 *    currentdate [":zone" <time-zone: string>]
 *                [COMPARATOR] [MATCH-TYPE]
 *                <date-part: string> <key-list: string-list>
 */

static bool
tst_currentdate_registered(struct sieve_validator *valdtr,
			   const struct sieve_extension *ext,
			   struct sieve_command_registration *cmd_reg);

const struct sieve_command_def currentdate_test = {
	.identifier = "currentdate",
	.type = SCT_TEST,
	.positional_args = 2,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.registered = tst_currentdate_registered,
	.validate = tst_date_validate,
	.generate = tst_date_generate,
};

/*
 * Tagged arguments
 */

/* Forward declarations */

static bool
tag_zone_validate(struct sieve_validator *valdtr,
		  struct sieve_ast_argument **arg, struct sieve_command *cmd);
static bool
tag_zone_generate(const struct sieve_codegen_env *cgenv,
		  struct sieve_ast_argument *arg, struct sieve_command *cmd);

/* Argument objects */

static const struct sieve_argument_def date_zone_tag = {
 	.identifier = "zone",
	.validate = tag_zone_validate,
	.generate = tag_zone_generate,
};

static const struct sieve_argument_def date_originalzone_tag = {
	.identifier = "originalzone",
	.validate = tag_zone_validate,
	.generate = tag_zone_generate,
};

/*
 * Date operation
 */

static bool
tst_date_operation_dump(const struct sieve_dumptime_env *denv,
			sieve_size_t *address);
static int
tst_date_operation_execute(const struct sieve_runtime_env *renv,
			   sieve_size_t *address);

const struct sieve_operation_def date_operation = {
	.mnemonic = "DATE",
	.ext_def = &date_extension,
	.code = EXT_DATE_OPERATION_DATE,
	.dump = tst_date_operation_dump,
	.execute = tst_date_operation_execute,
};

const struct sieve_operation_def currentdate_operation = {
	.mnemonic = "CURRENTDATE",
	.ext_def = &date_extension,
	.code = EXT_DATE_OPERATION_CURRENTDATE,
	.dump = tst_date_operation_dump,
	.execute = tst_date_operation_execute,
};

/*
 * Optional operands
 */

enum tst_date_optional {
	OPT_DATE_ZONE = SIEVE_AM_OPT_LAST,
	OPT_DATE_LAST,
};

/*
 * Tag implementation
 */

static bool
tag_zone_validate(struct sieve_validator *valdtr,
		  struct sieve_ast_argument **arg, struct sieve_command *cmd)
{
	struct sieve_ast_argument *tag = *arg;

	if ((bool)cmd->data) {
		if (sieve_command_is(cmd, date_test)) {
			sieve_argument_validate_error(
				valdtr, *arg,
				"multiple :zone or :originalzone arguments specified for "
				"the currentdate test");
		} else {
			sieve_argument_validate_error(
				valdtr, *arg,
				"multiple :zone arguments specified for the currentdate test");
		}
		return FALSE;
	}

	/* Skip tag */
 	*arg = sieve_ast_argument_next(*arg);

	/* :content tag has a string-list argument */
	if (sieve_argument_is(tag, date_zone_tag)) {

		/* Check syntax:
		 *   :zone <time-zone: string>
		 */
		if (!sieve_validate_tag_parameter(valdtr, cmd, tag, *arg,
						  NULL, 0, SAAT_STRING, FALSE))
			return FALSE;

		/* Check it */
		if (sieve_argument_is_string_literal(*arg)) {
			const char *zone = sieve_ast_argument_strc(*arg);

			if (!ext_date_parse_timezone(zone, NULL)) {
				sieve_argument_validate_warning(
					valdtr, *arg,
					"specified :zone argument '%s' is not a valid timezone",
					str_sanitize(zone, 40));
			}
		}

		/* Assign tag parameters */
		tag->parameters = *arg;
		*arg = sieve_ast_arguments_detach(*arg,1);
	}

	cmd->data = (void *)TRUE;
	return TRUE;
}

/*
 * Test registration
 */

static bool
tst_date_registered(struct sieve_validator *valdtr,
		    const struct sieve_extension *ext,
		    struct sieve_command_registration *cmd_reg)
{
	sieve_comparators_link_tag(valdtr, cmd_reg,
				   SIEVE_MATCH_OPT_COMPARATOR);
	sieve_match_types_link_tags(valdtr, cmd_reg,
				    SIEVE_MATCH_OPT_MATCH_TYPE);

	sieve_validator_register_tag(valdtr, cmd_reg, ext,
				     &date_zone_tag, OPT_DATE_ZONE);
	sieve_validator_register_tag(valdtr, cmd_reg, ext,
				     &date_originalzone_tag, OPT_DATE_ZONE);

	return TRUE;
}

static bool
tst_currentdate_registered(struct sieve_validator *valdtr,
			   const struct sieve_extension *ext,
			   struct sieve_command_registration *cmd_reg)
{
	sieve_comparators_link_tag(valdtr, cmd_reg,
				   SIEVE_MATCH_OPT_COMPARATOR);
	sieve_match_types_link_tags(valdtr, cmd_reg,
				    SIEVE_MATCH_OPT_MATCH_TYPE);

	sieve_validator_register_tag(valdtr, cmd_reg, ext,
				     &date_zone_tag, OPT_DATE_ZONE);

	return TRUE;
}

/*
 * Validation
 */

static bool
tst_date_validate(struct sieve_validator *valdtr, struct sieve_command *tst)
{
	struct sieve_ast_argument *arg = tst->first_positional;
	unsigned int arg_offset = 0 ;
	const struct sieve_match_type mcht_default =
		SIEVE_MATCH_TYPE_DEFAULT(is_match_type);
	const struct sieve_comparator cmp_default =
		SIEVE_COMPARATOR_DEFAULT(i_ascii_casemap_comparator);

	/* Check header name */

	if (sieve_command_is(tst, date_test)) {
		arg_offset = 1;

		if (!sieve_validate_positional_argument(
			valdtr, tst, arg, "header name", 1, SAAT_STRING))
			return FALSE;
		if (!sieve_validator_argument_activate(valdtr, tst, arg, FALSE))
			return FALSE;
		if (!sieve_command_verify_headers_argument(valdtr, arg))
			return FALSE;

		arg = sieve_ast_argument_next(arg);
	}

	/* Check date part */

	if (!sieve_validate_positional_argument(
		valdtr, tst, arg, "date part", arg_offset + 1, SAAT_STRING))
		return FALSE;
	if (!sieve_validator_argument_activate(valdtr, tst, arg, FALSE))
		return FALSE;

	if (sieve_argument_is_string_literal(arg)) {
		const char * part = sieve_ast_argument_strc(arg);

		if (ext_date_part_find(part) == NULL) {
			sieve_argument_validate_warning(
				valdtr, arg,
				"specified date part '%s' is not known",
				str_sanitize(part, 80));
		}
	}

	arg = sieve_ast_argument_next(arg);

	/* Check key list */

	if (!sieve_validate_positional_argument(
		valdtr, tst, arg, "key list", arg_offset + 2, SAAT_STRING_LIST))
		return FALSE;
	if (!sieve_validator_argument_activate(valdtr, tst, arg, FALSE))
		return FALSE;

	/* Validate the key argument to a specified match type */
	return sieve_match_type_validate(valdtr, tst, arg,
					 &mcht_default, &cmp_default);
}

/*
 * Code generation
 */

static bool
tst_date_generate(const struct sieve_codegen_env *cgenv,
		  struct sieve_command *tst)
{
	if (sieve_command_is(tst, date_test)) {
		sieve_operation_emit(cgenv->sblock, tst->ext,
				     &date_operation);
	} else if (sieve_command_is(tst, currentdate_test)) {
		sieve_operation_emit(cgenv->sblock, tst->ext,
				     &currentdate_operation);
	} else {
		i_unreached();
	}

	/* Generate arguments */
	return sieve_generate_arguments(cgenv, tst, NULL);
}

static bool
tag_zone_generate(const struct sieve_codegen_env *cgenv,
		  struct sieve_ast_argument *arg, struct sieve_command *cmd)
{
	if (arg->parameters == NULL) {
		sieve_opr_omitted_emit(cgenv->sblock);
		return TRUE;
	}
	return sieve_generate_argument_parameters(cgenv, cmd, arg);
}

/*
 * Code dump
 */

static bool
tst_date_operation_dump(const struct sieve_dumptime_env *denv,
			sieve_size_t *address)
{
	int opt_code = 0;
	const struct sieve_operation *op = denv->oprtn;

	sieve_code_dumpf(denv, "%s", sieve_operation_mnemonic(op));
	sieve_code_descend(denv);

	/* Handle any optional arguments */
	for (;;) {
		int opt;

		opt = sieve_message_opr_optional_dump(denv, address, &opt_code);
		if (opt < 0)
			return FALSE;
		if (opt == 0)
			break;

		switch (opt_code) {
		case OPT_DATE_ZONE:
			if (!sieve_opr_string_dump_ex(denv, address,
						      "zone", "ORIGINAL"))
				return FALSE;
			break;
		default:
			return FALSE;
		}
	}

	if (sieve_operation_is(op, date_operation) &&
	    !sieve_opr_string_dump(denv, address, "header name"))
		return FALSE;

	return (sieve_opr_string_dump(denv, address, "date part") &&
		sieve_opr_stringlist_dump(denv, address, "key list"));
}

/*
 * Code execution
 */

static int
tst_date_operation_execute(const struct sieve_runtime_env *renv,
			   sieve_size_t *address)
{
	const struct sieve_operation *op = renv->oprtn;
	int opt_code = 0;
	struct sieve_match_type mcht =
		SIEVE_MATCH_TYPE_DEFAULT(is_match_type);
	struct sieve_comparator cmp =
		SIEVE_COMPARATOR_DEFAULT(i_ascii_casemap_comparator);
	ARRAY_TYPE(sieve_message_override) svmos;
	string_t *date_part = NULL, *zone = NULL;
	struct sieve_stringlist *hdr_list = NULL, *hdr_value_list;
	struct sieve_stringlist *value_list, *key_list;
	bool zone_specified = FALSE, zone_literal = TRUE;
	const struct ext_date_part *dpart;
	int time_zone;
	int match, ret;

	/* Read optional operands */
	for (;;) {
		int opt;

		/* Optional operands */
		i_zero(&svmos);
		opt = sieve_message_opr_optional_read(
			renv, address, &opt_code, &ret, NULL,
			&mcht, &cmp, &svmos);
		if (opt < 0)
			return ret;
		if (opt == 0)
			break;

		switch (opt_code) {
		case OPT_DATE_ZONE:
			ret = sieve_opr_string_read_ex(
				renv, address, "zone", TRUE,
				&zone, &zone_literal);
			if (ret <= 0)
				return ret;
			zone_specified = TRUE;
			break;
		default:
			sieve_runtime_trace_error(
				renv, "unknown optional operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}
	}

	if (sieve_operation_is(op, date_operation)) {
		/* Read header name as stringlist */
		ret = sieve_opr_stringlist_read(renv, address, "header-name",
						&hdr_list);
		if (ret <= 0)
			return ret;
	}

	/* Read date part */
	ret = sieve_opr_string_read(renv, address, "date-part", &date_part);
	if (ret <= 0)
		return ret;

	/* Read key-list */
	ret = sieve_opr_stringlist_read(renv, address, "key-list", &key_list);
	if (ret <= 0)
		return ret;

	/* Determine what time zone to use in the result */
	if (!zone_specified) {
		time_zone = EXT_DATE_TIMEZONE_LOCAL;
	} else if (zone == NULL) {
		time_zone = EXT_DATE_TIMEZONE_ORIGINAL;
	} else if (!ext_date_parse_timezone(str_c(zone), &time_zone)) {
		if (!zone_literal) {
			sieve_runtime_warning(
				renv, NULL,
				"specified :zone argument '%s' is not a valid timezone "
				"(using local zone)",
				str_sanitize(str_c(zone), 40));
		}
		time_zone = EXT_DATE_TIMEZONE_LOCAL;
	}

	dpart = ext_date_part_find(str_c(date_part));
	if (dpart == NULL) {
		sieve_runtime_warning(
			renv, NULL,
			"specified date part argument '%s' is not known",
			str_sanitize(str_c(date_part), 40));
		sieve_interpreter_set_test_result(renv->interp, FALSE);
		return SIEVE_EXEC_OK;
	}

	/*
	 * Perform test
	 */

	if (sieve_operation_is(op, date_operation)) {
		sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS, "date test");

		/* Get header */
		sieve_runtime_trace_descend(renv);
		ret = sieve_message_get_header_fields(renv, hdr_list, &svmos,
						      FALSE, &hdr_value_list);
		if (ret <= 0)
			return ret;
		sieve_runtime_trace_ascend(renv);

		/* Create value stringlist */
		value_list = ext_date_stringlist_create(renv, hdr_value_list,
							time_zone, dpart);
	} else if (sieve_operation_is(op, currentdate_operation)) {
		/* Use time stamp recorded at the time the script first started.
		 */
		sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS,
				    "currentdatedate test");

		/* Create value stringlist */
		value_list = ext_date_stringlist_create(
			renv, NULL, time_zone, dpart);
	} else {
		i_unreached();
	}

	/* Perform match */
	match = sieve_match(renv, &mcht, &cmp, value_list, key_list, &ret);
	if (match < 0)
		return ret;

	/* Set test result for subsequent conditional jump */
	sieve_interpreter_set_test_result(renv->interp, match > 0);
	return SIEVE_EXEC_OK;
}
