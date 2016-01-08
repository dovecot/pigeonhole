/* Copyright (c) 2002-2016 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str-sanitize.h"
#include "istream.h"

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-commands.h"
#include "sieve-actions.h"
#include "sieve-stringlist.h"
#include "sieve-code.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-match.h"

#include "ext-metadata-common.h"

#include <ctype.h>

#define TST_METADATA_MAX_MATCH_SIZE SIEVE_MAX_STRING_LEN

/*
 * Test definitions
 */

/* Forward declarations */

static bool tst_metadata_registered
	(struct sieve_validator *valdtr, const struct sieve_extension *ext,
		struct sieve_command_registration *cmd_reg);
static bool tst_metadata_validate
	(struct sieve_validator *valdtr, struct sieve_command *tst);
static bool tst_metadata_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *cmd);

/* Metadata test
 *
 * Syntax:
 *   metadata [MATCH-TYPE] [COMPARATOR]
 *            <mailbox: string>
 *            <annotation-name: string> <key-list: string-list>
 */

const struct sieve_command_def metadata_test = {
	.identifier = "metadata",
	.type = SCT_TEST,
	.positional_args = 3,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.registered = tst_metadata_registered,
	.validate = tst_metadata_validate,
	.generate = tst_metadata_generate,
};

/* Servermetadata test
 *
 * Syntax:
 *   servermetadata [MATCH-TYPE] [COMPARATOR]
 *            <annotation-name: string> <key-list: string-list>
 */

const struct sieve_command_def servermetadata_test = {
	.identifier = "servermetadata",
	.type = SCT_TEST,
	.positional_args = 2,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.registered = tst_metadata_registered,
	.validate = tst_metadata_validate,
	.generate = tst_metadata_generate
};

/*
 * Opcode definitions
 */

static bool tst_metadata_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_metadata_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

/* Metadata operation */

const struct sieve_operation_def metadata_operation = {
	.mnemonic = "METADATA",
	.ext_def = &mboxmetadata_extension,
	.code = EXT_METADATA_OPERATION_METADATA,
	.dump = tst_metadata_operation_dump,
	.execute = tst_metadata_operation_execute
};

/* Servermetadata operation */

const struct sieve_operation_def servermetadata_operation = {
	.mnemonic = "SERVERMETADATA",
	.ext_def = &servermetadata_extension,
	.code = EXT_METADATA_OPERATION_METADATA,
	.dump = tst_metadata_operation_dump,
	.execute = tst_metadata_operation_execute
};

/*
 * Test registration
 */

static bool tst_metadata_registered
(struct sieve_validator *valdtr,
	const struct sieve_extension *ext ATTR_UNUSED,
	struct sieve_command_registration *cmd_reg)
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(valdtr, cmd_reg, SIEVE_MATCH_OPT_COMPARATOR);
	sieve_match_types_link_tags(valdtr, cmd_reg, SIEVE_MATCH_OPT_MATCH_TYPE);

	return TRUE;
}

/*
 * Test validation
 */

static bool tst_metadata_validate
(struct sieve_validator *valdtr, struct sieve_command *tst)
{
	struct sieve_ast_argument *arg = tst->first_positional;
	const struct sieve_match_type mcht_default =
		SIEVE_MATCH_TYPE_DEFAULT(is_match_type);
	const struct sieve_comparator cmp_default =
		SIEVE_COMPARATOR_DEFAULT(i_ascii_casemap_comparator);
	unsigned int arg_index = 1;
	const char *error;

	/* mailbox */
	if ( sieve_command_is(tst, metadata_test) ) {
		if ( !sieve_validate_positional_argument
			(valdtr, tst, arg, "mailbox", arg_index++, SAAT_STRING) ) {
			return FALSE;
		}

		if ( !sieve_validator_argument_activate(valdtr, tst, arg, FALSE) )
			return FALSE;

		/* Check name validity when mailbox argument is not a variable */
		if ( sieve_argument_is_string_literal(arg) ) {
			const char *mailbox = sieve_ast_argument_strc(arg), *error;

			if ( !sieve_mailbox_check_name(mailbox, &error) ) {
				sieve_argument_validate_warning
					(valdtr, arg, "%s test: "
						"invalid mailbox name `%s' specified: %s",
						sieve_command_identifier(tst),
						str_sanitize(mailbox, 256), error);
			}
		}

		arg = sieve_ast_argument_next(arg);
	}

	/* annotation-name */
	if ( !sieve_validate_positional_argument
		(valdtr, tst, arg, "annotation-name", arg_index++, SAAT_STRING) ) {
		return FALSE;
	}

	if ( !sieve_validator_argument_activate(valdtr, tst, arg, FALSE) )
		return FALSE;

	if ( sieve_argument_is_string_literal(arg) ) {
		string_t *aname = sieve_ast_argument_str(arg);

		if ( !imap_metadata_verify_entry_name(str_c(aname), &error) ) {
			char *lcerror = t_strdup_noconst(error);
			lcerror[0] = i_tolower(lcerror[0]);
			sieve_argument_validate_warning
				(valdtr, arg, "%s test: "
					"specified annotation name `%s' is invalid: %s",
					sieve_command_identifier(tst),
					str_sanitize(str_c(aname), 256), lcerror);
		}
	}

	arg = sieve_ast_argument_next(arg);

	/* key-list */
	if ( !sieve_validate_positional_argument
		(valdtr, tst, arg, "key-list", arg_index++, SAAT_STRING_LIST) ) {
		return FALSE;
	}

	if ( !sieve_validator_argument_activate(valdtr, tst, arg, FALSE) )
		return FALSE;

	/* Validate the key argument to a specified match type */
	return sieve_match_type_validate
		(valdtr, tst, arg, &mcht_default, &cmp_default);
}

/*
 * Test generation
 */

static bool tst_metadata_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *tst)
{
	if ( sieve_command_is(tst, metadata_test) ) {
		sieve_operation_emit
			(cgenv->sblock, tst->ext, &metadata_operation);
	} else if ( sieve_command_is(tst, servermetadata_test) ) {
		sieve_operation_emit
			(cgenv->sblock, tst->ext, &servermetadata_operation);
	} else {
		i_unreached();
	}

 	/* Generate arguments */
	if ( !sieve_generate_arguments(cgenv, tst, NULL) )
		return FALSE;

	return TRUE;
}

/*
 * Code dump
 */

static bool tst_metadata_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	bool metadata = sieve_operation_is(denv->oprtn, metadata_operation);

	if ( metadata )
		sieve_code_dumpf(denv, "METADATA");
	else
		sieve_code_dumpf(denv, "SERVERMETADATA");

	sieve_code_descend(denv);

	/* Handle any optional arguments */
	if ( sieve_match_opr_optional_dump(denv, address, NULL) != 0 )
 		return FALSE;

	if ( metadata && !sieve_opr_string_dump(denv, address, "mailbox") )
		return FALSE;

	return
		sieve_opr_string_dump(denv, address, "annotation-name") &&
		sieve_opr_stringlist_dump(denv, address, "key list");
}

/*
 * Code execution
 */

static inline const char *_lc_error(const char *error)
{
	char *lcerror = t_strdup_noconst(error);
	lcerror[0] = i_tolower(lcerror[0]);

	return lcerror;
}

static int tst_metadata_get_annotation
(const struct sieve_runtime_env *renv, const char *mailbox,
	const char *aname, const char **annotation_r)
{
	struct mail_user *user = renv->scriptenv->user;
	struct mailbox *box;
	struct imap_metadata_transaction *imtrans;
	struct mail_attribute_value avalue;
	int status, ret;

	*annotation_r = NULL;

	if ( user == NULL )
		return SIEVE_EXEC_OK;

	if ( mailbox != NULL ) {
		struct mail_namespace *ns;
		ns = mail_namespace_find(user->namespaces, mailbox);
		box = mailbox_alloc(ns->list, mailbox, 0);
		imtrans = imap_metadata_transaction_begin(box);
	} else {
		box = NULL;
		imtrans = imap_metadata_transaction_begin_server(user);
	}

	status = SIEVE_EXEC_OK;
	ret = imap_metadata_get(imtrans, aname, &avalue);
	if (ret < 0) {
		enum mail_error error_code;
		const char *error;

		error = imap_metadata_transaction_get_last_error
			(imtrans, &error_code);

		sieve_runtime_error(renv, NULL, "%s test: "
			"failed to retrieve annotation `%s': %s%s",
			(mailbox != NULL ? "metadata" : "servermetadata"),
			str_sanitize(aname, 256), _lc_error(error),
			(error_code == MAIL_ERROR_TEMP ? " (temporary failure)" : ""));

		status = ( error_code == MAIL_ERROR_TEMP ?
			SIEVE_EXEC_TEMP_FAILURE : SIEVE_EXEC_FAILURE );

	} else if (avalue.value != NULL) {
		*annotation_r = avalue.value;
	}
	(void)imap_metadata_transaction_commit(&imtrans, NULL, NULL);
	if ( box != NULL )
		mailbox_free(&box);
	return status;
}

static int tst_metadata_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	bool metadata = sieve_operation_is(renv->oprtn, metadata_operation);
	struct sieve_match_type mcht =
		SIEVE_MATCH_TYPE_DEFAULT(is_match_type);
	struct sieve_comparator cmp =
		SIEVE_COMPARATOR_DEFAULT(i_ascii_casemap_comparator);
	string_t *mailbox, *aname;
	struct sieve_stringlist *value_list, *key_list;
	const char *annotation = NULL, *error;
	int match, ret;

	/*
	 * Read operands
	 */

	/* Handle match-type and comparator operands */
	if ( sieve_match_opr_optional_read
		(renv, address, NULL, &ret, &cmp, &mcht) < 0 )
		return ret;

	/* Read mailbox */
	if ( metadata ) {
		if ( (ret=sieve_opr_string_read(renv, address, "mailbox", &mailbox)) <= 0 )
			return ret;
	}

	/* Read annotation-name */
	if ( (ret=sieve_opr_string_read
		(renv, address, "annotation-name", &aname)) <= 0 )
		return ret;

	/* Read key-list */
	if ( (ret=sieve_opr_stringlist_read
		(renv, address, "key-list", &key_list)) <= 0 )
		return ret;

	/*
	 * Perform operation
	 */

	if ( metadata )
		sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS, "metadata test");
	else
		sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS, "servermetadata test");
	sieve_runtime_trace_descend(renv);

	if ( !imap_metadata_verify_entry_name(str_c(aname), &error) ) {
		sieve_runtime_warning(renv, NULL, "%s test: "
			"specified annotation name `%s' is invalid: %s",
			(metadata ? "metadata" : "servermetadata"),
			str_sanitize(str_c(aname), 256), _lc_error(error));
		sieve_interpreter_set_test_result(renv->interp, FALSE);
		return SIEVE_EXEC_OK;
	}

	if ( metadata ) {
		if ( !sieve_mailbox_check_name(str_c(mailbox), &error) ) {
			sieve_runtime_warning(renv, NULL, "metadata test: "
				"invalid mailbox name `%s' specified: %s",
				str_sanitize(str_c(mailbox), 256), error);
			sieve_interpreter_set_test_result(renv->interp, FALSE);
			return SIEVE_EXEC_OK;
		}

		sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS,
			"retrieving annotation `%s' from mailbox `%s'",
			str_sanitize(str_c(aname), 256),
			str_sanitize(str_c(mailbox), 80));
	} else {
		sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS,
			"retrieving server annotation `%s'",
			str_sanitize(str_c(aname), 256));
	}

	/* Get annotation */
	if ( (ret=tst_metadata_get_annotation
		(renv, (metadata ? str_c(mailbox) : NULL), str_c(aname), &annotation))
			== SIEVE_EXEC_OK ) {
		/* Perform match */
		if ( annotation != NULL ) {
			/* Create value stringlist */
			value_list = sieve_single_stringlist_create_cstr(renv, annotation, FALSE);

			/* Perform match */
			if ( (match=sieve_match
				(renv, &mcht, &cmp, value_list, key_list, &ret)) < 0 )
				return ret;
		} else {
			match = 0;
		}
	}

	/* Set test result for subsequent conditional jump */
	if (ret == SIEVE_EXEC_OK)
		sieve_interpreter_set_test_result(renv->interp, match > 0);
	return ret;
}
