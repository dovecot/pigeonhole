/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "str-sanitize.h"
#include "message-date.h"

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-address-parts.h"
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

static bool tst_date_validate
	(struct sieve_validator *valdtr, struct sieve_command *tst);
static bool tst_date_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *ctx);
 
/* Date test
 *
 * Syntax:
 *    date [<":zone" <time-zone: string>> / ":originalzone"]
 *         [COMPARATOR] [MATCH-TYPE] <header-name: string>
 *         <date-part: string> <key-list: string-list>
 */

static bool tst_date_registered
	(struct sieve_validator *valdtr, const struct sieve_extension *ext,
		struct sieve_command_registration *cmd_reg);

const struct sieve_command_def date_test = { 
	"date", 
	SCT_TEST, 
	3, 0, FALSE, FALSE,
	tst_date_registered,
	NULL, 
	tst_date_validate, 
	tst_date_generate, 
	NULL 
};

/* Currentdate test
 * 
 * Syntax:
 *    currentdate [":zone" <time-zone: string>]
 *                [COMPARATOR] [MATCH-TYPE]
 *                <date-part: string> <key-list: string-list>
 */

static bool tst_currentdate_registered
	(struct sieve_validator *valdtr, const struct sieve_extension *ext,
		struct sieve_command_registration *cmd_reg);

const struct sieve_command_def currentdate_test = { 
	"currentdate", 
	SCT_TEST, 
	2, 0, FALSE, FALSE,
	tst_currentdate_registered,
	NULL, 
	tst_date_validate, 
	tst_date_generate, 
	NULL 
};

/* 
 * Tagged arguments 
 */

/* Forward declarations */

static bool tag_zone_validate
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
		struct sieve_command *cmd);
static bool tag_zone_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg,
		struct sieve_command *cmd);

/* Argument objects */

static const struct sieve_argument_def date_zone_tag = {
 	"zone",
	NULL,
	tag_zone_validate,
	NULL, NULL,
	tag_zone_generate
};

static const struct sieve_argument_def date_originalzone_tag = {
	"originalzone",
	NULL,
	tag_zone_validate,
	NULL, NULL,
	tag_zone_generate
};

/* 
 * Date operation 
 */

static bool tst_date_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_date_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def date_operation = { 
	"DATE",
	&date_extension,
	EXT_DATE_OPERATION_DATE,
	tst_date_operation_dump, 
	tst_date_operation_execute 
};

const struct sieve_operation_def currentdate_operation = { 
	"CURRENTDATE",
	&date_extension,
	EXT_DATE_OPERATION_CURRENTDATE,
	tst_date_operation_dump, 
	tst_date_operation_execute 
};

/*
 * Optional operands
 */

enum tst_date_optional {
	OPT_DATE_ZONE = SIEVE_MATCH_OPT_LAST,
	OPT_DATE_LAST
};

/*
 * Tag implementation
 */

static bool tag_zone_validate
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
    struct sieve_command *cmd)
{
	struct sieve_ast_argument *tag = *arg;

	if ( (bool) cmd->data ) {
		if ( sieve_command_is(cmd, date_test) ) {
			sieve_argument_validate_error(valdtr, *arg,
				"multiple :zone or :originalzone arguments specified for "
				"the currentdate test");
		} else {
			sieve_argument_validate_error(valdtr, *arg,
				"multiple :zone arguments specified for the currentdate test");
		}
		return FALSE;
	}

	/* Skip tag */
 	*arg = sieve_ast_argument_next(*arg);

	/* :content tag has a string-list argument */
	if ( sieve_argument_is(tag, date_zone_tag) ) {

		/* Check syntax:
		 *   :zone <time-zone: string>
		 */
		if ( !sieve_validate_tag_parameter
			(valdtr, cmd, tag, *arg, SAAT_STRING) ) {
			return FALSE;
		}

		/* Check it */
		if ( sieve_argument_is_string_literal(*arg) ) {
			const char *zone = sieve_ast_argument_strc(*arg);
	
			if ( !ext_date_parse_timezone(zone, NULL) ) {
				sieve_argument_validate_warning(valdtr, *arg,
					"specified :zone argument '%s' is not a valid timezone",
					str_sanitize(zone, 40));
			}		
		}
	
		/* Assign tag parameters */
		tag->parameters = *arg;
		*arg = sieve_ast_arguments_detach(*arg,1);
	} 

	cmd->data = (void *) TRUE;

	return TRUE;
}

/* 
 * Test registration 
 */

static bool tst_date_registered
(struct sieve_validator *valdtr, const struct sieve_extension *ext,
	struct sieve_command_registration *cmd_reg) 
{
	sieve_comparators_link_tag(valdtr, cmd_reg, SIEVE_MATCH_OPT_COMPARATOR);
	sieve_match_types_link_tags(valdtr, cmd_reg, SIEVE_MATCH_OPT_MATCH_TYPE);

	sieve_validator_register_tag
		(valdtr, cmd_reg, ext, &date_zone_tag, OPT_DATE_ZONE);
	sieve_validator_register_tag
		(valdtr, cmd_reg, ext, &date_originalzone_tag, OPT_DATE_ZONE);

	return TRUE;
}

static bool tst_currentdate_registered
(struct sieve_validator *valdtr, const struct sieve_extension *ext,
	struct sieve_command_registration *cmd_reg) 
{
	sieve_comparators_link_tag(valdtr, cmd_reg, SIEVE_MATCH_OPT_COMPARATOR);
	sieve_match_types_link_tags(valdtr, cmd_reg, SIEVE_MATCH_OPT_MATCH_TYPE);

	sieve_validator_register_tag
		(valdtr, cmd_reg, ext, &date_zone_tag, OPT_DATE_ZONE);

	return TRUE;
}

/* 
 * Validation 
 */
 
static bool tst_date_validate
(struct sieve_validator *valdtr, struct sieve_command *tst) 
{
	struct sieve_ast_argument *arg = tst->first_positional;
	unsigned int arg_offset = 0 ;
	const struct sieve_match_type mcht_default = 
		SIEVE_MATCH_TYPE_DEFAULT(is_match_type);
	const struct sieve_comparator cmp_default = 
		SIEVE_COMPARATOR_DEFAULT(i_ascii_casemap_comparator);
		
	/* Check header name */

	if ( sieve_command_is(tst, date_test) ) {
		arg_offset = 1;

		if ( !sieve_validate_positional_argument
			(valdtr, tst, arg, "header name", 1, SAAT_STRING) ) {
			return FALSE;
		}
	
		if ( !sieve_validator_argument_activate(valdtr, tst, arg, FALSE) )
			return FALSE;

		if ( !sieve_command_verify_headers_argument(valdtr, arg) )
    	    return FALSE;

		arg = sieve_ast_argument_next(arg);
	}

	/* Check date part */

	if ( !sieve_validate_positional_argument
		(valdtr, tst, arg, "date part", arg_offset + 1, SAAT_STRING) ) {
		return FALSE;
	}
	
	if ( !sieve_validator_argument_activate(valdtr, tst, arg, FALSE) )
		return FALSE;

	arg = sieve_ast_argument_next(arg);

	/* Check key list */
		
	if ( !sieve_validate_positional_argument
		(valdtr, tst, arg, "key list", arg_offset + 2, SAAT_STRING_LIST) ) {
		return FALSE;
	}

	if ( !sieve_validator_argument_activate(valdtr, tst, arg, FALSE) )
		return FALSE;
	
	/* Validate the key argument to a specified match type */
	return sieve_match_type_validate
		(valdtr, tst, arg, &mcht_default, &cmp_default); 
}

/* 
 * Code generation 
 */

static bool tst_date_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *tst) 
{
	if ( sieve_command_is(tst, date_test) )
		sieve_operation_emit(cgenv->sbin, tst->ext, &date_operation);
	else if ( sieve_command_is(tst, currentdate_test) )
		sieve_operation_emit(cgenv->sbin, tst->ext, &currentdate_operation);
	else
		i_unreached();

	/* Generate arguments */  	
	return sieve_generate_arguments(cgenv, tst, NULL);
}

static bool tag_zone_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg,
    struct sieve_command *cmd)
{
	if ( arg->parameters == NULL ) {
		sieve_opr_omitted_emit(cgenv->sbin);
		return TRUE;
	}

	return sieve_generate_argument_parameters(cgenv, cmd, arg);
}

/* 
 * Code dump 
 */

static bool tst_date_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	int opt_code = 0;
	const struct sieve_operation *op = &denv->oprtn;
	struct sieve_operand operand;

	sieve_code_dumpf(denv, "%s", sieve_operation_mnemonic(op));
	sieve_code_descend(denv);
	
	/* Handle any optional arguments */
  do {
		if ( !sieve_match_dump_optional_operands(denv, address, &opt_code) )
			return FALSE;

		switch ( opt_code ) {
		case SIEVE_MATCH_OPT_END:
			break;
		case OPT_DATE_ZONE:
			if ( !sieve_operand_read(denv->sbin, address, &operand) ) {
				sieve_code_dumpf(denv, "ERROR: INVALID OPERAND");
				return FALSE;
			}				

			if ( sieve_operand_is_omitted(&operand) ) {
				sieve_code_dumpf(denv, "zone: ORIGINAL");
			} else {
				if ( !sieve_opr_string_dump_data
					(denv, &operand, address, "zone") )
					return FALSE;
			}
			break;
    default:
			return FALSE;
		}
	} while ( opt_code != SIEVE_MATCH_OPT_END );

	if ( sieve_operation_is(op, date_operation) &&
		!sieve_opr_string_dump(denv, address, "header name") )
		return FALSE;

	return
		sieve_opr_string_dump(denv, address, "date part") && 
		sieve_opr_stringlist_dump(denv, address, "key list");
}

/* 
 * Code execution 
 */

static int tst_date_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{	
	const struct sieve_operation *op = &renv->oprtn;
	bool result = TRUE, zone_specified = FALSE, got_date = FALSE, matched = FALSE;
	int opt_code = 0;
	const struct sieve_message_data *msgdata = renv->msgdata;
	struct sieve_match_type mcht = 
		SIEVE_MATCH_TYPE_DEFAULT(is_match_type);
	struct sieve_comparator cmp = 
		SIEVE_COMPARATOR_DEFAULT(i_ascii_casemap_comparator);
	struct sieve_operand operand;
	struct sieve_match_context *mctx;
	string_t *header_name = NULL, *date_part = NULL, *zone = NULL;
	struct sieve_coded_stringlist *key_list;
	time_t date_value, local_time;
	struct tm *date_tm;
	const char *part_value;
	int local_zone = 0, original_zone = 0, wanted_zone = 0;
	int ret;
	
	/* Read optional operands */
	do {
		if ( (ret=sieve_match_read_optional_operands
			(renv, address, &opt_code, &cmp, &mcht)) <= 0 )
			return ret;

		switch ( opt_code ) {
		case SIEVE_MATCH_OPT_END:
			break;
		case OPT_DATE_ZONE:
			if ( !sieve_operand_read(renv->sbin, address, &operand) ) {
				sieve_runtime_trace_error(renv, "invalid operand");
				return SIEVE_EXEC_BIN_CORRUPT;
			}

			if ( !sieve_operand_is_omitted(&operand) ) {
				if ( !sieve_opr_string_read_data
					(renv, &operand, address, &zone) ) {
					sieve_runtime_trace_error(renv, "invalid zone operand");
					return SIEVE_EXEC_BIN_CORRUPT;
				}
			}

			zone_specified = TRUE;
			break;
		default:
			sieve_runtime_trace_error(renv, "unknown optional operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}
	} while ( opt_code != SIEVE_MATCH_OPT_END );


	if ( sieve_operation_is(op, date_operation) ) {
		/* Read header name */
		if ( !sieve_opr_string_read(renv, address, &header_name) ) {
			sieve_runtime_trace_error(renv, "invalid header-name operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}
	}

	/* Read date part */
	if ( !sieve_opr_string_read(renv, address, &date_part) ) {
		sieve_runtime_trace_error(renv, "invalid date-part operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
		
	/* Read key-list */
	if ( (key_list=sieve_opr_stringlist_read(renv, address)) == NULL ) {
		sieve_runtime_trace_error(renv, "invalid key-list operand");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	/* Perform test */

	sieve_runtime_trace(renv, "%s test", sieve_operation_mnemonic(op));

	/* Get the date value */

	local_time = ext_date_get_current_date(renv, &local_zone);

	if ( sieve_operation_is(op, date_operation) ) {
		const char *header_value;
		const char *date_string;

		/* Get date from the message */

		/* Read first header
		 *   NOTE: need something for index extension to hook into some time. 
		 */
		if ( (ret=mail_get_first_header
			(msgdata->mail, str_c(header_name), &header_value)) > 0 ) {

			/* Extract the date string value */
			date_string = strrchr(header_value, ';');
			if ( date_string == NULL )
				/* Direct header value */
				date_string = header_value;
			else {
				/* Delimited by ';', e.g. a Received: header */
				date_string++; 
			}

			/* Parse the date value */
			if ( message_date_parse((const unsigned char *) date_string,
				strlen(date_string), &date_value, &original_zone) ) {
				got_date = TRUE;
			}
		}
	} else if ( sieve_operation_is(op, currentdate_operation) ) {
		/* Use time stamp recorded at the time the script first started */

		date_value = local_time;
		original_zone = local_zone;
		got_date = TRUE;

	} else {
		i_unreached();
	}

	if ( got_date ) {
		/* Apply wanted timezone */

		if ( !zone_specified )
			wanted_zone = local_zone;
		else if ( zone == NULL 
			|| !ext_date_parse_timezone(str_c(zone), &wanted_zone) ) {

			/* FIXME: warn about parse failures */
			wanted_zone = original_zone;
		}

		date_value += wanted_zone * 60;

		/* Convert timestamp to struct tm */

		if ( (date_tm=gmtime(&date_value)) == NULL ) {
			got_date = FALSE;
		} else {
			/* Extract the date part */
			part_value = ext_date_part_extract
				(str_c(date_part), date_tm, wanted_zone);
		}
	}

	/* Initialize match */
	mctx = sieve_match_begin(renv->interp, &mcht, &cmp, NULL, key_list); 	
	
	if ( got_date && part_value != NULL ) {		
		/* Match value */
		if ( (ret=sieve_match_value(mctx, part_value, strlen(part_value))) < 0 )
			result = FALSE;
		else
			matched = ret > 0;
	}

	/* Finish match */
	if ( (ret=sieve_match_end(&mctx)) < 0 ) 
		result = FALSE;
	else
		matched = ( ret > 0 || matched );
	
	/* Set test result for subsequent conditional jump */
	if ( result ) {
		sieve_interpreter_set_test_result(renv->interp, matched);
		return SIEVE_EXEC_OK;
	}	

	sieve_runtime_trace_error(renv, "invalid string-list item");
	return SIEVE_EXEC_BIN_CORRUPT;
}
