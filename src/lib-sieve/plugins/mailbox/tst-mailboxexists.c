/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str-sanitize.h"
#include "mail-storage.h"
#include "mail-namespace.h"

#include "sieve-common.h"
#include "sieve-actions.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-stringlist.h"
#include "sieve-code.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-mailbox-common.h"

/*
 * Mailboxexists command
 *
 * Syntax:
 *    mailboxexists <mailbox-names: string-list>
 */

static bool
tst_mailboxexists_validate(struct sieve_validator *valdtr,
			   struct sieve_command *tst);
static bool
tst_mailboxexists_generate(const struct sieve_codegen_env *cgenv,
			   struct sieve_command *ctx);

const struct sieve_command_def mailboxexists_test = {
	.identifier = "mailboxexists",
	.type = SCT_TEST,
	.positional_args = 1,
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = tst_mailboxexists_validate,
	.generate = tst_mailboxexists_generate,
};

/*
 * Mailboxexists operation
 */

static bool
tst_mailboxexists_operation_dump(const struct sieve_dumptime_env *denv,
				 sieve_size_t *address);
static int
tst_mailboxexists_operation_execute(const struct sieve_runtime_env *renv,
				    sieve_size_t *address);

const struct sieve_operation_def mailboxexists_operation = {
	.mnemonic = "MAILBOXEXISTS",
	.ext_def = &mailbox_extension,
	.dump = tst_mailboxexists_operation_dump,
	.execute = tst_mailboxexists_operation_execute,
};

/*
 * Test validation
 */

struct _validate_context {
	struct sieve_validator *valdtr;
	struct sieve_command *tst;
};

static int
tst_mailboxexists_mailbox_validate(void *context,
				   struct sieve_ast_argument *arg)
{
	struct _validate_context *valctx =
		(struct _validate_context *)context;

	if (sieve_argument_is_string_literal(arg)) {
		const char *mailbox = sieve_ast_argument_strc(arg), *error;

		if (!sieve_mailbox_check_name(mailbox, &error)) {
			sieve_argument_validate_warning(
				valctx->valdtr, arg, "%s test: "
				"invalid mailbox name '%s' specified: %s",
				sieve_command_identifier(valctx->tst),
				str_sanitize(mailbox, 256), error);
		}
	}

	return 1;
}

static bool
tst_mailboxexists_validate(struct sieve_validator *valdtr,
			   struct sieve_command *tst)
{
	struct sieve_ast_argument *arg = tst->first_positional;
	struct sieve_ast_argument *aarg;
	struct _validate_context valctx;

	if (!sieve_validate_positional_argument(
		valdtr, tst, arg, "mailbox-names", 1, SAAT_STRING_LIST))
		return FALSE;

	if (!sieve_validator_argument_activate(valdtr, tst, arg, FALSE))
		return FALSE;

	aarg = arg;
	i_zero(&valctx);
	valctx.valdtr = valdtr;
	valctx.tst = tst;

	return (sieve_ast_stringlist_map(
		&aarg, &valctx,
		tst_mailboxexists_mailbox_validate) >= 0);
}

/*
 * Test generation
 */

static bool
tst_mailboxexists_generate(const struct sieve_codegen_env *cgenv,
			   struct sieve_command *tst)
{
	sieve_operation_emit(cgenv->sblock, tst->ext, &mailboxexists_operation);

 	/* Generate arguments */
	return sieve_generate_arguments(cgenv, tst, NULL);
}

/*
 * Code dump
 */

static bool
tst_mailboxexists_operation_dump(const struct sieve_dumptime_env *denv,
				 sieve_size_t *address)
{
	sieve_code_dumpf(denv, "MAILBOXEXISTS");
	sieve_code_descend(denv);

	return sieve_opr_stringlist_dump(denv, address, "mailbox-names");
}

/*
 * Code execution
 */

static int
tst_mailboxexists_test_mailbox(const struct sieve_runtime_env *renv,
			       const char *mailbox, bool trace,
			       bool *all_exist_r)
{
	const struct sieve_execute_env *eenv = renv->exec_env;
	struct mailbox *box;
	const char *error;

	/* Check validity of mailbox name */
	if (!sieve_mailbox_check_name(mailbox, &error)) {
		sieve_runtime_warning(
			renv, NULL, "mailboxexists test: "
			"invalid mailbox name '%s' specified: %s",
			str_sanitize(mailbox, 256), error);
		*all_exist_r = FALSE;
		return SIEVE_EXEC_OK;
	}

	/* Open the box */
	box = mailbox_alloc_for_user(eenv->scriptenv->user,
				     mailbox,
				     MAILBOX_FLAG_POST_SESSION);

	if (mailbox_open(box) < 0) {
		if (trace) {
			sieve_runtime_trace(
				renv, 0,
				"mailbox '%s' cannot be opened",
				str_sanitize(mailbox, 80));
		}
		mailbox_free(&box);
		*all_exist_r = FALSE;
		return SIEVE_EXEC_OK;
	}

	/* Also fail when it is readonly */
	if (mailbox_is_readonly(box)) {
		if (trace) {
			sieve_runtime_trace(
				renv, 0,
				"mailbox '%s' is read-only",
				str_sanitize(mailbox, 80));
		}
		mailbox_free(&box);
		*all_exist_r = FALSE;
		return SIEVE_EXEC_OK;
	}

	/* FIXME: check acl for 'p' or 'i' ACL permissions as
	   required by RFC */

	if (trace) {
		sieve_runtime_trace(
			renv, 0, "mailbox '%s' exists",
			str_sanitize(mailbox, 80));
	}

	/* Close mailbox */
	mailbox_free(&box);
	return SIEVE_EXEC_OK;
}

static int
tst_mailboxexists_operation_execute(const struct sieve_runtime_env *renv,
				    sieve_size_t *address)
{
	const struct sieve_execute_env *eenv = renv->exec_env;
	struct sieve_stringlist *mailbox_names;
	string_t *mailbox_item;
	bool trace = FALSE;
	bool all_exist = TRUE;
	int ret;

	/*
	 * Read operands
	 */

	/* Read notify uris */
	ret = sieve_opr_stringlist_read(renv, address, "mailbox-names",
					&mailbox_names);
	if (ret <= 0)
		return ret;

	/*
	 * Perform operation
	 */

	if (sieve_runtime_trace_active(renv, SIEVE_TRLVL_TESTS)) {
		sieve_runtime_trace(renv, 0, "mailboxexists test");
		sieve_runtime_trace_descend(renv);

		trace = sieve_runtime_trace_active(renv, SIEVE_TRLVL_MATCHING);
	}

	if (eenv->scriptenv->user == NULL) {
		sieve_runtime_trace(renv, 0, "no mail user; yield true");
		sieve_interpreter_set_test_result(renv->interp, TRUE);
		return SIEVE_EXEC_OK;
	}

	mailbox_item = NULL;
	while (all_exist &&
	       (ret = sieve_stringlist_next_item(mailbox_names,
						 &mailbox_item)) > 0) {
		const char *mailbox = str_c(mailbox_item);

		ret = tst_mailboxexists_test_mailbox(renv, mailbox,
						     trace, &all_exist);
		if (ret <= 0)
			return ret;
	}

	if (ret < 0) {
		sieve_runtime_trace_error(
			renv, "invalid mailbox name item");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	if (trace) {
		if (all_exist) {
			sieve_runtime_trace(renv, 0,
					    "all mailboxes are available");
		} else {
			sieve_runtime_trace(renv, 0,
					    "some mailboxes are unavailable");
		}
	}

	sieve_interpreter_set_test_result(renv->interp, all_exist);
	return SIEVE_EXEC_OK;
}
