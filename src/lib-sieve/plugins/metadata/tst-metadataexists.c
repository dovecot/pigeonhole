/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str-sanitize.h"
#include "mail-storage.h"
#include "mail-namespace.h"

#include "sieve-common.h"
#include "sieve-commands.h"
#include "sieve-actions.h"
#include "sieve-stringlist.h"
#include "sieve-code.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-metadata-common.h"

#include <ctype.h>


/*
 * Command definitions
 */

/* Forward declarations */

static bool tst_metadataexists_validate
	(struct sieve_validator *valdtr, struct sieve_command *tst);
static bool tst_metadataexists_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *ctx);

/* Metadataexists command
 *
 * Syntax:
 *    metadataexists <mailbox: string> <annotation-names: string-list>
 */

static bool tst_metadataexists_validate
	(struct sieve_validator *valdtr, struct sieve_command *tst);
static bool tst_metadataexists_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *ctx);

const struct sieve_command_def metadataexists_test = {
	.identifier = "metadataexists",
	.type = SCT_TEST,
	.positional_args = 2,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = tst_metadataexists_validate,
	.generate = tst_metadataexists_generate,
};

/* Servermetadataexists command
 *
 * Syntax:
 *    servermetadataexists <annotation-names: string-list>
 */

const struct sieve_command_def servermetadataexists_test = {
	.identifier = "servermetadataexists",
	.type = SCT_TEST,
	.positional_args = 1,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = tst_metadataexists_validate,
	.generate = tst_metadataexists_generate,
};

/*
 * Opcode definitions
 */

static bool tst_metadataexists_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_metadataexists_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

/* Metadata operation */

const struct sieve_operation_def metadataexists_operation = {
	.mnemonic = "METADATAEXISTS",
	.ext_def = &mboxmetadata_extension,
	.code = EXT_METADATA_OPERATION_METADATAEXISTS,
	.dump = tst_metadataexists_operation_dump,
	.execute = tst_metadataexists_operation_execute
};

/* Mailboxexists operation */

const struct sieve_operation_def servermetadataexists_operation = {
	.mnemonic = "SERVERMETADATAEXISTS",
	.ext_def = &servermetadata_extension,
	.code = EXT_METADATA_OPERATION_METADATAEXISTS,
	.dump = tst_metadataexists_operation_dump,
	.execute = tst_metadataexists_operation_execute
};

/*
 * Test validation
 */

struct _validate_context {
	struct sieve_validator *valdtr;
	struct sieve_command *tst;
};

static int tst_metadataexists_annotation_validate
(void *context, struct sieve_ast_argument *arg)
{
	struct _validate_context *valctx =
		(struct _validate_context *)context;

	if ( sieve_argument_is_string_literal(arg) ) {
		const char *aname = sieve_ast_strlist_strc(arg);
		const char *error;

		if ( !imap_metadata_verify_entry_name(aname, &error) ) {
			char *lcerror = t_strdup_noconst(error);
			lcerror[0] = i_tolower(lcerror[0]);
			sieve_argument_validate_warning
				(valctx->valdtr, arg, "%s test: "
					"specified annotation name `%s' is invalid: %s",
					sieve_command_identifier(valctx->tst),
					str_sanitize(aname, 256), lcerror);
		}
	}

	return 1; /* Can't check at compile time */
}

static bool tst_metadataexists_validate
(struct sieve_validator *valdtr, struct sieve_command *tst)
{
	struct sieve_ast_argument *arg = tst->first_positional;
	struct sieve_ast_argument *aarg; 
	struct _validate_context valctx;
	unsigned int arg_index = 1;

	if ( sieve_command_is(tst, metadataexists_test) ) {
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

	if ( !sieve_validate_positional_argument
		(valdtr, tst, arg, "annotation-names", arg_index++, SAAT_STRING_LIST) ) {
		return FALSE;
	}

	if ( !sieve_validator_argument_activate(valdtr, tst, arg, FALSE) )
		return FALSE;

	aarg = arg;
	i_zero(&valctx);
	valctx.valdtr = valdtr;
	valctx.tst = tst;

	return (sieve_ast_stringlist_map(&aarg,
		(void*)&valctx, tst_metadataexists_annotation_validate) >= 0);
}

/*
 * Test generation
 */

static bool tst_metadataexists_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *tst)
{
	if ( sieve_command_is(tst, metadataexists_test) ) {
		sieve_operation_emit
			(cgenv->sblock, tst->ext, &metadataexists_operation);
	} else if ( sieve_command_is(tst, servermetadataexists_test) ) {
		sieve_operation_emit
			(cgenv->sblock, tst->ext, &servermetadataexists_operation);
	} else {
		i_unreached();
	}

 	/* Generate arguments */
	return sieve_generate_arguments(cgenv, tst, NULL);
}

/*
 * Code dump
 */

static bool tst_metadataexists_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	bool metadata = sieve_operation_is(denv->oprtn, metadataexists_operation);

	if ( metadata )
		sieve_code_dumpf(denv, "METADATAEXISTS");
	else
		sieve_code_dumpf(denv, "SERVERMETADATAEXISTS");

	sieve_code_descend(denv);

	if ( metadata && !sieve_opr_string_dump(denv, address, "mailbox") )
		return FALSE;

	return
		sieve_opr_stringlist_dump(denv, address, "annotation-names");
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

static int tst_metadataexists_check_annotations
(const struct sieve_runtime_env *renv, const char *mailbox,
	struct sieve_stringlist *anames, bool *all_exist_r)
{
	struct mail_user *user = renv->scriptenv->user;
	struct mailbox *box = NULL;
	struct imap_metadata_transaction *imtrans;
	string_t *aname;
	bool all_exist = TRUE;
	int ret, sret, status;

	*all_exist_r = FALSE;

	if ( user == NULL )
		return SIEVE_EXEC_OK;

	if ( mailbox != NULL ) {
		struct mail_namespace *ns;
		ns = mail_namespace_find(user->namespaces, mailbox);
		box = mailbox_alloc(ns->list, mailbox, 0);
		imtrans = imap_metadata_transaction_begin(box);
	} else {
		imtrans = imap_metadata_transaction_begin_server(user);
	}

	if ( mailbox != NULL ) {
		sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS,
			"checking annotations of mailbox `%s':",
			str_sanitize(mailbox, 80));
	} else {
		sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS,
			"checking server annotations");
	}

	aname = NULL;
	status = SIEVE_EXEC_OK;
	while ( all_exist &&
		(sret=sieve_stringlist_next_item(anames, &aname)) > 0 ) {
		struct mail_attribute_value avalue;
		const char *error;

		if ( !imap_metadata_verify_entry_name(str_c(aname), &error) ) {
			sieve_runtime_warning(renv, NULL, "%s test: "
				"specified annotation name `%s' is invalid: %s",
				(mailbox != NULL ? "metadataexists" : "servermetadataexists"),
				str_sanitize(str_c(aname), 256), _lc_error(error));
			all_exist = FALSE;
			break;;
		}

		ret = imap_metadata_get(imtrans, str_c(aname), &avalue);
		if (ret < 0) {
			enum mail_error error_code;
			const char *error;

			error = imap_metadata_transaction_get_last_error
				(imtrans, &error_code);
			sieve_runtime_error(renv, NULL, "%s test: "
				"failed to retrieve annotation `%s': %s%s",
				(mailbox != NULL ? "metadataexists" : "servermetadataexists"),
				str_sanitize(str_c(aname), 256), _lc_error(error),
				(error_code == MAIL_ERROR_TEMP ? " (temporary failure)" : ""));

			all_exist = FALSE;
			status = ( error_code == MAIL_ERROR_TEMP ?
				SIEVE_EXEC_TEMP_FAILURE : SIEVE_EXEC_FAILURE );
			break;

		} else if (avalue.value == NULL && avalue.value_stream == NULL) {
			all_exist = FALSE;
			sieve_runtime_trace(renv, 0,
				"annotation `%s': not found", str_c(aname));
			break;

		} else {
			sieve_runtime_trace(renv, 0,
				"annotation `%s': found", str_c(aname));
		}
	}

	if ( sret < 0 ) {
		sieve_runtime_trace_error
			(renv, "invalid annotation name stringlist item");
		status = SIEVE_EXEC_BIN_CORRUPT;
	}

	(void)imap_metadata_transaction_commit(&imtrans, NULL, NULL);
	if ( box != NULL )
		mailbox_free(&box);

	*all_exist_r = all_exist;
	return status;
}

static int tst_metadataexists_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	bool metadata = sieve_operation_is(renv->oprtn, metadataexists_operation);
	struct sieve_stringlist *anames;
	string_t *mailbox;
	bool trace = FALSE, all_exist = TRUE;
	const char *error;
	int ret;

	/*
	 * Read operands
	 */

	/* Read mailbox */
	if ( metadata ) {
		if ( (ret=sieve_opr_string_read(renv, address, "mailbox", &mailbox)) <= 0 )
			return ret;
	}

	/* Read annotation names */
	if ( (ret=sieve_opr_stringlist_read
		(renv, address, "annotation-names", &anames)) <= 0 )
		return ret;

	/*
	 * Perform operation
	 */

	if ( metadata && !sieve_mailbox_check_name(str_c(mailbox), &error) ) {
		sieve_runtime_warning(renv, NULL, "metadata test: "
			"invalid mailbox name `%s' specified: %s",
			str_sanitize(str_c(mailbox), 256), error);
		sieve_interpreter_set_test_result(renv->interp, FALSE);
		return SIEVE_EXEC_OK;
	}

	if ( sieve_runtime_trace_active(renv, SIEVE_TRLVL_TESTS) ) {
		if ( metadata )
			sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS, "metadataexists test");
		else
			sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS, "servermetadataexists test");

		sieve_runtime_trace_descend(renv);

		trace = sieve_runtime_trace_active(renv, SIEVE_TRLVL_MATCHING);
	}

	if ( (ret=tst_metadataexists_check_annotations
		(renv, (metadata ? str_c(mailbox) : NULL), anames,
			&all_exist)) <= 0 )
		return ret;

	if ( trace ) {
		if ( all_exist )
			sieve_runtime_trace(renv, 0, "all annotations exist");
		else
			sieve_runtime_trace(renv, 0, "some annotations do not exist");
	}

	sieve_interpreter_set_test_result(renv->interp, all_exist);
	return SIEVE_EXEC_OK;
}
