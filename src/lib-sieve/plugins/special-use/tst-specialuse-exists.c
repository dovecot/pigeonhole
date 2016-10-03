/* Copyright (c) 2019 Pigeonhole authors, see the included COPYING file
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

#include "ext-special-use-common.h"

/*
 * specialuse_exists command
 *
 * Syntax:
 *    specialuse_exists [<mailbox: string>]
 *                      <special-use-flags: string-list>
 */

static bool
tst_specialuse_exists_validate(struct sieve_validator *valdtr,
			       struct sieve_command *tst);
static bool
tst_specialuse_exists_generate(const struct sieve_codegen_env *cgenv,
			       struct sieve_command *ctx);

const struct sieve_command_def specialuse_exists_test = {
	.identifier = "specialuse_exists",
	.type = SCT_TEST,
	.positional_args = -1, /* We check positional arguments ourselves */
	.subtests = 0,
	.block_allowed = FALSE,
	.block_required = FALSE,
	.validate = tst_specialuse_exists_validate,
	.generate = tst_specialuse_exists_generate
};

/*
 * Mailboxexists operation
 */

static bool
tst_specialuse_exists_operation_dump(const struct sieve_dumptime_env *denv,
				     sieve_size_t *address);
static int
tst_specialuse_exists_operation_execute(const struct sieve_runtime_env *renv,
					sieve_size_t *address);

const struct sieve_operation_def specialuse_exists_operation = {
	.mnemonic = "SPECIALUSE_EXISTS",
	.ext_def = &special_use_extension,
	.dump = tst_specialuse_exists_operation_dump,
	.execute = tst_specialuse_exists_operation_execute
};

/*
 * Test validation
 */

struct _validate_context {
	struct sieve_validator *valdtr;
	struct sieve_command *tst;
};

static int
tst_specialuse_exists_flag_validate(void *context,
				    struct sieve_ast_argument *arg)
{
	struct _validate_context *valctx = (struct _validate_context *)context;

	if (sieve_argument_is_string_literal(arg)) {
		const char *flag = sieve_ast_argument_strc(arg);

		if (!ext_special_use_flag_valid(flag)) {
			sieve_argument_validate_error(
				valctx->valdtr, arg, "%s test: "
				"invalid special-use flag `%s' specified",
				sieve_command_identifier(valctx->tst),
				str_sanitize(flag, 64));
		}
	}

	return 1;
}

static bool
tst_specialuse_exists_validate(struct sieve_validator *valdtr,
			       struct sieve_command *tst)
{
	struct sieve_ast_argument *arg = tst->first_positional;
	struct sieve_ast_argument *arg2;
	struct sieve_ast_argument *aarg;
	struct _validate_context valctx;

	if (arg == NULL) {
		sieve_command_validate_error(
			valdtr, tst, "the %s %s expects at least one argument, "
			"but none was found",
			sieve_command_identifier(tst),
			sieve_command_type_name(tst));
		return FALSE;
	}

	if (sieve_ast_argument_type(arg) != SAAT_STRING &&
	    sieve_ast_argument_type(arg) != SAAT_STRING_LIST) {
		sieve_argument_validate_error(
			valdtr, arg,
			"the %s %s expects either a string (mailbox) or "
			"a string-list (special-use flags) as first argument, "
			"but %s was found",
			sieve_command_identifier(tst),
			sieve_command_type_name(tst),
			sieve_ast_argument_name(arg));
		return FALSE;
	}

	arg2 = sieve_ast_argument_next(arg);
	if (arg2 != NULL) {
		/* First, check syntax sanity */
		if (sieve_ast_argument_type(arg) != SAAT_STRING) {
			sieve_argument_validate_error(
				valdtr, arg,
				"if a second argument is specified for the %s %s, the first "
				"must be a string (mailbox), but %s was found",
				sieve_command_identifier(tst),
				sieve_command_type_name(tst),
				sieve_ast_argument_name(arg));
			return FALSE;
		}
		if (!sieve_validator_argument_activate(valdtr, tst, arg, FALSE))
			return FALSE;

		if (sieve_ast_argument_type(arg2) != SAAT_STRING &&
		    sieve_ast_argument_type(arg2) != SAAT_STRING_LIST)
		{
			sieve_argument_validate_error(
				valdtr, arg2,
				"the %s %s expects a string list (special-use flags) as "
				"second argument when two arguments are specified, "
				"but %s was found",
				sieve_command_identifier(tst),
				sieve_command_type_name(tst),
				sieve_ast_argument_name(arg2));
			return FALSE;
		}
	} else
		arg2 = arg;

	if (!sieve_validator_argument_activate(valdtr, tst, arg2, FALSE))
		return FALSE;

	aarg = arg2;
	memset(&valctx, 0, sizeof(valctx));
	valctx.valdtr = valdtr;
	valctx.tst = tst;

	return (sieve_ast_stringlist_map(
		&aarg,	(void*)&valctx,
		tst_specialuse_exists_flag_validate) >= 0);
}

/*
 * Test generation
 */

static bool
tst_specialuse_exists_generate(const struct sieve_codegen_env *cgenv,
			       struct sieve_command *tst)
{
	struct sieve_ast_argument *arg = tst->first_positional;
	struct sieve_ast_argument *arg2;

	sieve_operation_emit(cgenv->sblock,
		tst->ext, &specialuse_exists_operation);

	/* Generate arguments */
	arg2 = sieve_ast_argument_next(arg);
	if (arg2 != NULL) {
		if (!sieve_generate_argument(cgenv, arg, tst))
			return FALSE;
	} else {
		sieve_opr_omitted_emit(cgenv->sblock);
		arg2 = arg;
	}
	return sieve_generate_argument(cgenv, arg2, tst);
}

/*
 * Code dump
 */

static bool
tst_specialuse_exists_operation_dump(const struct sieve_dumptime_env *denv,
				     sieve_size_t *address)
{
	struct sieve_operand oprnd;

	sieve_code_dumpf(denv, "SPECIALUSE_EXISTS");
	sieve_code_descend(denv);

	sieve_code_mark(denv);
	if (!sieve_operand_read(denv->sblock, address, NULL, &oprnd)) {
		sieve_code_dumpf(denv, "ERROR: INVALID OPERAND");
		return FALSE;
	}

	if (!sieve_operand_is_omitted(&oprnd)) {
		return (sieve_opr_string_dump_data(denv, &oprnd,
						   address, "mailbox") &&
		        sieve_opr_stringlist_dump(denv, address,
						  "special-use-flags"));
	}

	return sieve_opr_stringlist_dump(denv, address, "special-use-flags");
}

/*
 * Code execution
 */

static int
tst_specialuse_find_mailbox(const struct sieve_runtime_env *renv,
			    const char *mailbox, struct mailbox **box_r)
{
	const struct sieve_execute_env *eenv = renv->exec_env;
	struct mail_user *user = eenv->scriptenv->user;
	struct mailbox *box;
	bool trace = sieve_runtime_trace_active(renv, SIEVE_TRLVL_MATCHING);
	enum mail_error error_code;
	const char *error;

	*box_r = NULL;

	if (user == NULL)
		return 0;

	/* Open the box */
	box = mailbox_alloc_for_user(user, mailbox, MAILBOX_FLAG_POST_SESSION);
	if (mailbox_open(box) < 0) {
		error = mailbox_get_last_error(box, &error_code);

		if (trace) {
			sieve_runtime_trace(
				renv, 0, "mailbox `%s' cannot be opened: %s",
				str_sanitize(mailbox, 256), error);
		}

		mailbox_free(&box);

		if (error_code == MAIL_ERROR_TEMP) {
			sieve_runtime_error(
				renv, NULL,	"specialuse_exists test: "
				"failed to open mailbox `%s': %s",
				str_sanitize(mailbox, 256), error);
			return -1;
		}
		return 0;
	}

	/* Also fail when it is readonly */
	if (mailbox_is_readonly(box)) {
		if (trace) {
			sieve_runtime_trace(
				renv, 0, "mailbox `%s' is read-only",
				str_sanitize(mailbox, 256));
		}

		mailbox_free(&box);
		return 0;
	}

	*box_r = box;
	return 1;
}

static int
tst_specialuse_find_specialuse(const struct sieve_runtime_env *renv,
			       const char *special_use)
{
	const struct sieve_execute_env *eenv = renv->exec_env;
	struct mail_user *user = eenv->scriptenv->user;
	struct mailbox *box;
	bool trace = sieve_runtime_trace_active(renv, SIEVE_TRLVL_MATCHING);
	enum mail_error error_code;
	const char *error;

	if (user == NULL)
		return 0;

	/* Open the box */
	box = mailbox_alloc_for_user(user, special_use,
				     (MAILBOX_FLAG_POST_SESSION |
				      MAILBOX_FLAG_SPECIAL_USE));
	if (mailbox_open(box) < 0) {
		error = mailbox_get_last_error(box, &error_code);

		if (trace) {
			sieve_runtime_trace(
				renv, 0, "mailbox with special-use flag `%s' "
				"cannot be opened: %s",
				str_sanitize(special_use, 64), error);
		}

		mailbox_free(&box);

		if (error_code == MAIL_ERROR_TEMP) {
			sieve_runtime_error(
				renv, NULL, "specialuse_exists test: "
				"failed to open mailbox with special-use flag`%s': %s",
				str_sanitize(special_use, 64), error);
			return -1;
		}
		return 0;
	}

	/* Also fail when it is readonly */
	if (mailbox_is_readonly(box)) {
		if (trace) {
			sieve_runtime_trace(
				renv, 0,
				"mailbox with special-use flag `%s' is read-only",
				str_sanitize(special_use, 64));
		}

		mailbox_free(&box);
		return 0;
	}

	mailbox_free(&box);
	return 1;
}

static int
tst_specialuse_exists_operation_execute(const struct sieve_runtime_env *renv,
					sieve_size_t *address)
{
	struct sieve_operand oprnd;
	struct sieve_stringlist *special_use_flags;
	string_t *mailbox;
	struct mailbox *box = NULL;
	bool trace = FALSE, all_exist = TRUE;
	int ret;

	/*
	 * Read operands
	 */

	/* Read bare operand (two types possible) */
	if ((ret = sieve_operand_runtime_read(renv, address,
					      NULL, &oprnd)) <= 0)
		return ret;

	/* Mailbox operand (optional) */
	mailbox = NULL;
	if (!sieve_operand_is_omitted(&oprnd)) {
		/* Read the mailbox operand */
		if ((ret = sieve_opr_string_read_data(
			renv, &oprnd, address, "mailbox", &mailbox)) <= 0)
			return ret;

		/* Read flag list */
		if ((ret = sieve_opr_stringlist_read(
			renv, address, "special-use-flags",
			&special_use_flags)) <= 0)
			return ret;

	/* Flag-list operand */
	} else {
		/* Read flag list */
		if ((ret = sieve_opr_stringlist_read(
			renv, address, "special-use-flags",
			&special_use_flags)) <= 0)
			return ret;
	}

	/*
	 * Perform operation
	 */

	if (sieve_runtime_trace_active(renv, SIEVE_TRLVL_TESTS)) {
		sieve_runtime_trace(renv, 0, "specialuse_exists test");
		sieve_runtime_trace_descend(renv);

		trace = sieve_runtime_trace_active(renv, SIEVE_TRLVL_MATCHING);
	}

	if (mailbox != NULL) {
		if (tst_specialuse_find_mailbox(renv, str_c(mailbox), &box) < 0)
			return SIEVE_EXEC_TEMP_FAILURE;
	}

	ret = 0;
	if (box == NULL && mailbox != NULL) {
		all_exist = FALSE;
		sieve_runtime_trace(renv, 0,
			"mailbox `%s' is not accessible",
			str_sanitize(str_c(mailbox), 80));
	} else {
		string_t *special_use_flag;

		if (mailbox != NULL) {
			sieve_runtime_trace(
				renv, 0, "mailbox `%s' is accessible",
				str_sanitize(str_c(mailbox), 80));
		}

		special_use_flag = NULL;
		while ((ret = sieve_stringlist_next_item(
			special_use_flags, &special_use_flag)) > 0) {
			const char *use_flag = str_c(special_use_flag);

			if (!ext_special_use_flag_valid(use_flag)) {
				sieve_runtime_error(
					renv, NULL, "specialuse_exists test: "
					"invalid special-use flag `%s' specified",
					str_sanitize(use_flag, 64));
				if (box != NULL) {
					/* Close mailbox */
					mailbox_free(&box);
				}
				return SIEVE_EXEC_FAILURE;
			}

			if (box != NULL) {
				/* Mailbox has this SPECIAL-USE flag? */
				if (!mailbox_has_special_use(box, use_flag)) {
					all_exist = FALSE;
					break;
				}
			} else {
				/* Is there mailbox with this SPECIAL-USE flag? */
				if ((ret = tst_specialuse_find_specialuse(
					renv, use_flag)) <= 0) {
					if (ret < 0)
						return SIEVE_EXEC_TEMP_FAILURE;
					all_exist = FALSE;
					break;
				}
			}

			if (trace) {
				sieve_runtime_trace(
					renv, 0, "special-use flag `%s' exists",
					str_sanitize(use_flag, 80));
			}
		}
	}

	if (box != NULL) {
		/* Close mailbox */
		mailbox_free(&box);
	}

	if (ret < 0) {
		sieve_runtime_trace_error(
			renv, "invalid special-use flag item");
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	if (trace) {
		if (all_exist) {
			sieve_runtime_trace(
				renv, 0, "all special-use flags are set");
		} else {
			sieve_runtime_trace(
				renv, 0, "some special-use are not set");
		}
	}

	sieve_interpreter_set_test_result(renv->interp, all_exist);
	return SIEVE_EXEC_OK;
}
