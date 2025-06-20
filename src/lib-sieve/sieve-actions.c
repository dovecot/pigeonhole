/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "strfuncs.h"
#include "ioloop.h"
#include "hostpid.h"
#include "str-sanitize.h"
#include "unichar.h"
#include "istream.h"
#include "istream-header-filter.h"
#include "ostream.h"
#include "smtp-params.h"
#include "mail-storage.h"
#include "message-date.h"
#include "message-size.h"

#include "rfc2822.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-result.h"
#include "sieve-actions.h"
#include "sieve-message.h"
#include "sieve-smtp.h"

/*
 * Action execution environment
 */

struct event_passthrough *
sieve_action_create_finish_event(const struct sieve_action_exec_env *aenv)
{
	struct event_passthrough *e =
		event_create_passthrough(aenv->event)->
		set_name("sieve_action_finished");

	return e;
}

/*
 * Action instance
 */

bool sieve_action_is_executed(const struct sieve_action *act,
			      struct sieve_result *result)
{
	unsigned int cur_exec_seq = sieve_result_get_exec_seq(result);

	i_assert(act->exec_seq <= cur_exec_seq);
	return (act->exec_seq < cur_exec_seq);
}

/*
 * Side-effect operand
 */

const struct sieve_operand_class
sieve_side_effect_operand_class = { "SIDE-EFFECT" };

bool sieve_opr_side_effect_dump(const struct sieve_dumptime_env *denv,
				sieve_size_t *address)
{
	struct sieve_side_effect seffect;
	const struct sieve_side_effect_def *sdef;

	if (!sieve_opr_object_dump(denv, &sieve_side_effect_operand_class,
				   address, &seffect.object))
		return FALSE;

	sdef = seffect.def =
		(const struct sieve_side_effect_def *)seffect.object.def;

	if (sdef->dump_context != NULL) {
		sieve_code_descend(denv);
		if (!sdef->dump_context(&seffect, denv, address))
			return FALSE;
		sieve_code_ascend(denv);
	}
	return TRUE;
}

int sieve_opr_side_effect_read(const struct sieve_runtime_env *renv,
			       sieve_size_t *address,
			       struct sieve_side_effect *seffect)
{
	const struct sieve_side_effect_def *sdef;
	int ret;

	seffect->context = NULL;

	if (!sieve_opr_object_read(renv, &sieve_side_effect_operand_class,
				   address, &seffect->object))
		return SIEVE_EXEC_BIN_CORRUPT;

	sdef = seffect->def =
		(const struct sieve_side_effect_def *)seffect->object.def;

	if (sdef->read_context != NULL &&
	    (ret = sdef->read_context(seffect, renv, address,
				      &seffect->context)) <= 0)
		return ret;
	return SIEVE_EXEC_OK;
}

/*
 * Optional operands
 */

int sieve_action_opr_optional_dump(const struct sieve_dumptime_env *denv,
				   sieve_size_t *address, signed int *opt_code)
{
	signed int _opt_code = 0;
	bool final = FALSE, opok = TRUE;

	if (opt_code == NULL) {
		opt_code = &_opt_code;
		final = TRUE;
	}

	while (opok) {
		int opt;

		opt = sieve_opr_optional_dump(denv, address, opt_code);
		if (opt <= 0)
			return opt;

		if (*opt_code == SIEVE_OPT_SIDE_EFFECT)
			opok = sieve_opr_side_effect_dump(denv, address);
		else
			return (final ? -1 : 1);
	}

	return -1;
}

int sieve_action_opr_optional_read(const struct sieve_runtime_env *renv,
				   sieve_size_t *address,
				   signed int *opt_code, int *exec_status,
				   struct sieve_side_effects_list **list)
{
	signed int _opt_code = 0;
	bool final = FALSE;
	int ret;

	if (opt_code == NULL) {
		opt_code = &_opt_code;
		final = TRUE;
	}

	if (exec_status != NULL)
		*exec_status = SIEVE_EXEC_OK;

	for (;;) {
		int opt;

		opt = sieve_opr_optional_read(renv, address, opt_code);
		if (opt <= 0) {
			if (opt < 0 && exec_status != NULL)
				*exec_status = SIEVE_EXEC_BIN_CORRUPT;
			return opt;
		}

		if (*opt_code == SIEVE_OPT_SIDE_EFFECT) {
			struct sieve_side_effect seffect;

			i_assert(list != NULL);

			ret = sieve_opr_side_effect_read(renv, address,
							 &seffect);
			if (ret <= 0) {
				if (exec_status != NULL)
					*exec_status = ret;
				return -1;
			}

			if (*list == NULL) {
				*list = sieve_side_effects_list_create(
					renv->result);
			}

			sieve_side_effects_list_add(*list, &seffect);
		} else {
			if (final) {
				sieve_runtime_trace_error(
					renv, "invalid optional operand");
				if (exec_status != NULL)
					*exec_status = SIEVE_EXEC_BIN_CORRUPT;
				return -1;
			}
			return 1;
		}
	}

	i_unreached();
}

/*
 * Store action
 */

/* Forward declarations */

static bool
act_store_equals(const struct sieve_script_env *senv,
		 const struct sieve_action *act1,
		 const struct sieve_action *act2);

static int
act_store_check_duplicate(const struct sieve_runtime_env *renv,
			  const struct sieve_action *act,
			  const struct sieve_action *act_other);
static void
act_store_print(const struct sieve_action *action,
		const struct sieve_result_print_env *rpenv, bool *keep);

static int
act_store_start(const struct sieve_action_exec_env *aenv, void **tr_context);
static int
act_store_execute(const struct sieve_action_exec_env *aenv, void *tr_context,
		  bool *keep);
static int
act_store_commit(const struct sieve_action_exec_env *aenv, void *tr_context);
static void
act_store_rollback(const struct sieve_action_exec_env *aenv, void *tr_context,
		   bool success);

/* Action object */

const struct sieve_action_def act_store = {
	.name = "store",
	.flags =
		SIEVE_ACTFLAG_TRIES_DELIVER |
		SIEVE_ACTFLAG_MAIL_STORAGE,
	.equals = act_store_equals,
	.check_duplicate = act_store_check_duplicate,
	.print = act_store_print,
	.start = act_store_start,
	.execute = act_store_execute,
	.commit = act_store_commit,
	.rollback = act_store_rollback,
};

/* API */

int sieve_act_store_add_to_result(const struct sieve_runtime_env *renv,
				  const char *name,
				  struct sieve_side_effects_list *seffects,
				  const char *mailbox)
{
	pool_t pool;
	struct act_store_context *act;

	/* Add redirect action to the result */
	pool = sieve_result_pool(renv->result);
	act = p_new(pool, struct act_store_context, 1);
	act->mailbox = p_strdup(pool, mailbox);

	return sieve_result_add_action(renv, NULL, name, &act_store, seffects,
				       act, 0, TRUE);
}

void sieve_act_store_add_flags(const struct sieve_action_exec_env *aenv,
			       void *tr_context, const char *const *keywords,
			       enum mail_flags flags)
{
	struct act_store_transaction *trans =
		(struct act_store_transaction *)tr_context;

	i_assert(trans != NULL);

	/* Assign mail keywords for subsequent mailbox_copy() */
	if (*keywords != NULL) {
		const char *const *kw;

		if (!array_is_created(&trans->keywords)) {
			pool_t pool = sieve_result_pool(aenv->result);
			p_array_init(&trans->keywords, pool, 2);
		}

		kw = keywords;
		while (*kw != NULL) {
			array_append(&trans->keywords, kw, 1);
			kw++;
		}
	}

	/* Assign mail flags for subsequent mailbox_copy() */
	trans->flags |= flags;

	trans->flags_altered = TRUE;
}

/* Equality */

static bool
act_store_equals(const struct sieve_script_env *senv,
		 const struct sieve_action *act1,
		 const struct sieve_action *act2)
{
	struct act_store_context *st_ctx1 =
		(act1 == NULL ?
		 NULL : (struct act_store_context *)act1->context);
	struct act_store_context *st_ctx2 =
		(act2 == NULL ?
		 NULL : (struct act_store_context *)act2->context);
	const char *mailbox1, *mailbox2;

	/* FIXME: consider namespace aliases */

	if (st_ctx1 == NULL && st_ctx2 == NULL)
		return TRUE;

	mailbox1 = (st_ctx1 == NULL ?
		    SIEVE_SCRIPT_DEFAULT_MAILBOX(senv) : st_ctx1->mailbox);
	mailbox2 = (st_ctx2 == NULL ?
		    SIEVE_SCRIPT_DEFAULT_MAILBOX(senv) : st_ctx2->mailbox);

	if (strcmp(mailbox1, mailbox2) == 0)
		return TRUE;

	return (strcasecmp(mailbox1, "INBOX") == 0 &&
		strcasecmp(mailbox2, "INBOX") == 0);
}

/* Result verification */

static int
act_store_check_duplicate(const struct sieve_runtime_env *renv,
			  const struct sieve_action *act,
			  const struct sieve_action *act_other)
{
	const struct sieve_execute_env *eenv = renv->exec_env;

	return (act_store_equals(eenv->scriptenv, act, act_other) ? 1 : 0);
}

/* Result printing */

static void
act_store_print(const struct sieve_action *action,
		const struct sieve_result_print_env *rpenv, bool *keep)
{
	struct act_store_context *ctx =
		(struct act_store_context *)action->context;
	const char *mailbox;

	mailbox = (ctx == NULL ?
		   SIEVE_SCRIPT_DEFAULT_MAILBOX(rpenv->scriptenv) :
			ctx->mailbox);

	sieve_result_action_printf(rpenv, "store message in folder: %s",
				   str_sanitize(mailbox, 128));

	*keep = FALSE;
}

/* Action implementation */

void sieve_act_store_get_storage_error(const struct sieve_action_exec_env *aenv,
				       struct act_store_transaction *trans)
{
	pool_t pool = sieve_result_pool(aenv->result);

	trans->error = p_strdup(pool,
		mailbox_get_last_internal_error(trans->box,
						&trans->error_code));
}

static bool
act_store_mailbox_alloc(const struct sieve_action_exec_env *aenv,
		        const char *mailbox, struct mailbox **box_r,
			enum mail_error *error_code_r, const char **error_r)
{
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct mailbox *box;
	struct mail_storage **storage = &(eenv->exec_status->last_storage);
	enum mailbox_flags flags = MAILBOX_FLAG_POST_SESSION;

	*box_r = NULL;
	*error_code_r = MAIL_ERROR_NONE;
	*error_r = NULL;

	if (!uni_utf8_str_is_valid(mailbox)) {
		/* Just a precaution; already (supposed to be) checked at
		   compiletime/runtime.
		 */
		*error_r = t_strdup_printf("mailbox name not utf-8: %s",
					   mailbox);
		*error_code_r = MAIL_ERROR_PARAMS;
		return FALSE;
	}

	if (eenv->scriptenv->mailbox_autocreate)
		flags |= MAILBOX_FLAG_AUTO_CREATE;
	if (eenv->scriptenv->mailbox_autosubscribe)
		flags |= MAILBOX_FLAG_AUTO_SUBSCRIBE;
	*box_r = box = mailbox_alloc_for_user(eenv->scriptenv->user, mailbox,
					      flags);
	*storage = mailbox_get_storage(box);

	return TRUE;
}

static int
act_store_start(const struct sieve_action_exec_env *aenv, void **tr_context)
{
	const struct sieve_action *action = aenv->action;
	struct act_store_context *ctx =
		(struct act_store_context *)action->context;
	const struct sieve_execute_env *eenv = aenv->exec_env;
	const struct sieve_script_env *senv = eenv->scriptenv;
	struct act_store_transaction *trans;
	struct mailbox *box = NULL;
	pool_t pool = sieve_result_pool(aenv->result);
	const char *error = NULL;
	enum mail_error error_code = MAIL_ERROR_NONE;
	bool disabled = FALSE, alloc_failed = FALSE;

	/* If context is NULL, the store action is the result of (implicit)
	   keep.
	 */
	if (ctx == NULL) {
		ctx = p_new(pool, struct act_store_context, 1);
		ctx->mailbox =
			p_strdup(pool, SIEVE_SCRIPT_DEFAULT_MAILBOX(senv));
	}

	e_debug(aenv->event, "Start storing into mailbox %s", ctx->mailbox);

	/* Open the requested mailbox */

	/* NOTE: The caller of the sieve library is allowed to leave user set
	   to NULL. This implementation will then skip actually storing the
	   message.
	 */
	if (senv->user != NULL) {
		if (!act_store_mailbox_alloc(aenv, ctx->mailbox, &box,
					     &error_code, &error))
			alloc_failed = TRUE;
	} else {
		disabled = TRUE;
	}

	/* Create transaction context */
	trans = p_new(pool, struct act_store_transaction, 1);

	trans->context = ctx;
	trans->box = box;
	trans->flags = 0;

	trans->mailbox_name = ctx->mailbox;
	trans->mailbox_identifier =
		p_strdup_printf(pool, "'%s'", str_sanitize(ctx->mailbox, 256));

	trans->disabled = disabled;

	if (alloc_failed) {
		trans->error = p_strdup(pool, error);
		trans->error_code = error_code;
		e_debug(aenv->event, "Failed to open mailbox %s: %s",
			trans->mailbox_identifier, trans->error);
	} else {
		trans->error_code = MAIL_ERROR_NONE;
	}

	*tr_context = trans;

	switch (trans->error_code) {
	case MAIL_ERROR_NONE:
	case MAIL_ERROR_NOTFOUND:
		return SIEVE_EXEC_OK;
	case MAIL_ERROR_TEMP:
		return SIEVE_EXEC_TEMP_FAILURE;
	default:
		break;
	}
	return SIEVE_EXEC_FAILURE;
}

static struct mail_keywords *
act_store_keywords_create(const struct sieve_action_exec_env *aenv,
			  ARRAY_TYPE(const_string) *keywords,
			  struct mailbox *box, bool create_empty)
{
	struct mail_keywords *box_keywords = NULL;
	const char *const *kwds = NULL;

	if (!array_is_created(keywords) || array_count(keywords) == 0) {
		if (!create_empty)
			return NULL;
	} else {
		ARRAY_TYPE(const_string) valid_keywords;
		const char *error;
		unsigned int count, i;

		kwds = array_get(keywords, &count);
		t_array_init(&valid_keywords, count);

		for (i = 0; i < count; i++) {
			if (mailbox_keyword_is_valid(box, kwds[i], &error)) {
				array_append(&valid_keywords, &kwds[i], 1);
				continue;
			}

			sieve_result_warning(aenv,
				"specified IMAP keyword '%s' is invalid "
				"(ignored): %s",  str_sanitize(kwds[i], 64),
				sieve_error_from_external(error));
		}

		array_append_zero(keywords);
		kwds = array_idx(keywords, 0);
	}

	if (mailbox_keywords_create(box, kwds, &box_keywords) < 0) {
		sieve_result_error(
			aenv, "invalid keywords set for stored message");
		return NULL;
	}

	return box_keywords;
}

static bool have_equal_keywords(struct mail *mail, struct mail_keywords *new_kw)
{
	const ARRAY_TYPE(keyword_indexes) *old_kw_arr =
		mail_get_keyword_indexes(mail);
	const unsigned int *old_kw;
	unsigned int i, j;

	if (array_count(old_kw_arr) != new_kw->count)
		return FALSE;
	if (new_kw->count == 0)
		return TRUE;

	old_kw = array_front(old_kw_arr);
	for (i = 0; i < new_kw->count; i++) {
		/* new_kw->count equals old_kw's count and it's easier to use */
		for (j = 0; j < new_kw->count; j++) {
			if (old_kw[j] == new_kw->idx[i])
				break;
		}
		if (j == new_kw->count)
			return FALSE;
	}
	return TRUE;
}

static int
act_store_execute(const struct sieve_action_exec_env *aenv, void *tr_context,
		  bool *keep)
{
	const struct sieve_action *action = aenv->action;
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct act_store_transaction *trans =
		(struct act_store_transaction *)tr_context;
	struct mail *mail = (action->mail != NULL ?
			     action->mail : eenv->msgdata->mail);
	struct mail_save_context *save_ctx;
	struct mail_keywords *keywords = NULL;
	struct mailbox *box;
	bool backends_equal = FALSE;
	int status = SIEVE_EXEC_OK;

	/* Verify transaction */
	if (trans == NULL)
		return SIEVE_EXEC_FAILURE;
	box = trans->box;

	/* Check whether we need to do anything */
	if (trans->disabled) {
		e_debug(aenv->event, "Skip storing into mailbox %s",
			trans->mailbox_identifier);
		*keep = FALSE;
		return SIEVE_EXEC_OK;
	}

	/* Exit early if mailbox is not available */
	if (box == NULL)
		return SIEVE_EXEC_FAILURE;

	e_debug(aenv->event, "Execute storing into mailbox %s",
		trans->mailbox_identifier);

	/* Mark attempt to use storage. Can only get here when all previous
	   actions succeeded.
	 */
	eenv->exec_status->last_storage = mailbox_get_storage(box);

	/* Open the mailbox (may already be open) */
	if (trans->error_code == MAIL_ERROR_NONE) {
		if (mailbox_open(box) < 0) {
			sieve_act_store_get_storage_error(aenv, trans);
			e_debug(aenv->event, "Failed to open mailbox %s: %s",
				trans->mailbox_identifier, trans->error);
		}
	}

	/* Exit early if transaction already failed */
 	switch (trans->error_code) {
	case MAIL_ERROR_NONE:
		break;
	case MAIL_ERROR_TEMP:
		return SIEVE_EXEC_TEMP_FAILURE;
	default:
		return SIEVE_EXEC_FAILURE;
	}

	/* If the message originates from the target mailbox, only update the
	   flags and keywords (if not read-only)
	 */
	if (mailbox_backends_equal(box, mail->box)) {
		backends_equal = TRUE;
	} else {
		struct mail *real_mail;

		if (mail_get_backend_mail(mail, &real_mail) < 0)
			return SIEVE_EXEC_FAILURE;
		if (real_mail != mail &&
		    mailbox_backends_equal(box, real_mail->box))
			backends_equal = TRUE;
	}
	if (backends_equal) {
		trans->redundant = TRUE;

		if (trans->flags_altered && !mailbox_is_readonly(mail->box)) {
			keywords = act_store_keywords_create(
				aenv, &trans->keywords, mail->box, TRUE);

			if (keywords != NULL) {
				if (!have_equal_keywords(mail, keywords)) {
					eenv->exec_status->significant_action_executed = TRUE;
					mail_update_keywords(mail, MODIFY_REPLACE, keywords);
				}
				mailbox_keywords_unref(&keywords);
			}

			if ((mail_get_flags(mail) & MAIL_FLAGS_NONRECENT) != trans->flags) {
				eenv->exec_status->significant_action_executed = TRUE;
				mail_update_flags(mail, MODIFY_REPLACE, trans->flags);
			}
		}
		e_debug(aenv->event, "Updated existing mail in mailbox %s",
			trans->mailbox_identifier);
		return SIEVE_EXEC_OK;

	/* If the message is modified, only store it in the source mailbox when
	   it is not opened read-only. Mail structs of modified messages have
	   their own mailbox, unrelated to the orignal mail, so this case needs
	   to be handled separately.
	 */
	} else if (mail != eenv->msgdata->mail &&
		   mailbox_is_readonly(eenv->msgdata->mail->box) &&
		   (mailbox_backends_equal(box, eenv->msgdata->mail->box))) {
		e_debug(aenv->event,
			"Not modifying exsiting mail in read-only mailbox %s",
			trans->mailbox_identifier);
		trans->redundant = TRUE;
		return SIEVE_EXEC_OK;
	}

	/* Mark attempt to store in default mailbox */
	if (strcmp(trans->context->mailbox,
		   SIEVE_SCRIPT_DEFAULT_MAILBOX(eenv->scriptenv)) == 0)
		eenv->exec_status->tried_default_save = TRUE;

	/* Start mail transaction */
	trans->mail_trans = mailbox_transaction_begin(
		box, MAILBOX_TRANSACTION_FLAG_EXTERNAL, __func__);

	/* Store the message */
	save_ctx = mailbox_save_alloc(trans->mail_trans);

	/* Apply keywords and flags that side-effects may have added */
	if (trans->flags_altered) {
		keywords = act_store_keywords_create(aenv, &trans->keywords,
						     box, FALSE);

		if (trans->flags != 0 || keywords != NULL) {
			eenv->exec_status->significant_action_executed = TRUE;
			mailbox_save_set_flags(save_ctx, trans->flags, keywords);
		}
	} else {
		mailbox_save_copy_flags(save_ctx, mail);
	}

	if (mailbox_save_using_mail(&save_ctx, mail) < 0) {
		sieve_act_store_get_storage_error(aenv, trans);
		e_debug(aenv->event, "Failed to save to mailbox %s: %s",
			trans->mailbox_identifier, trans->error);

		status = (trans->error_code == MAIL_ERROR_TEMP ?
			  SIEVE_EXEC_TEMP_FAILURE : SIEVE_EXEC_FAILURE);
	} else {
		e_debug(aenv->event, "Saving to mailbox %s successful so far",
			trans->mailbox_identifier);
		eenv->exec_status->significant_action_executed = TRUE;
	}

	/* Deallocate keywords */
 	if (keywords != NULL)
 		mailbox_keywords_unref(&keywords);

	/* Cancel implicit keep if all went well so far */
	*keep = (status < SIEVE_EXEC_OK);

	return status;
}

static void
act_store_log_status(struct act_store_transaction *trans,
		     const struct sieve_action_exec_env *aenv,
		     bool rolled_back, bool status)
{
	const char *mailbox_name = trans->mailbox_name;
	const char *mailbox_identifier = trans->mailbox_identifier;

	if (trans->box != NULL) {
		const char *mailbox_vname = str_sanitize(mailbox_get_vname(trans->box), 128);

		if (strcmp(trans->mailbox_name, mailbox_vname) != 0) {
			mailbox_identifier = t_strdup_printf(
				"%s (%s)", mailbox_identifier,
				str_sanitize(mailbox_vname, 256));
		}
	}

	/* Store disabled? */
	if (trans->disabled) {
		sieve_result_global_log(aenv, "store into mailbox %s skipped",
					mailbox_identifier);
	/* Store redundant? */
	} else if (trans->redundant) {
		sieve_result_global_log(aenv, "left message in mailbox %s",
					mailbox_identifier);
	/* Store failed? */
	} else if (!status) {
		const char *errstr;
		enum mail_error error_code;

		if (trans->error == NULL)
			sieve_act_store_get_storage_error(aenv, trans);

		errstr = trans->error;
		error_code = trans->error_code;

		if (error_code == MAIL_ERROR_NOQUOTA) {
			/* Never log quota problems as error in global log */
			sieve_result_global_log_error(
				aenv, "failed to store into mailbox %s: %s",
				mailbox_identifier, errstr);
		} else if (error_code == MAIL_ERROR_NOTFOUND ||
			   error_code == MAIL_ERROR_PARAMS ||
			   error_code == MAIL_ERROR_PERM) {
			sieve_result_error(
				aenv, "failed to store into mailbox %s: %s",
				mailbox_identifier, errstr);
		} else {
			sieve_result_global_error(
				aenv, "failed to store into mailbox %s: %s",
				mailbox_identifier, errstr);
		}
	/* Store aborted? */
	} else if (rolled_back) {
		if (!aenv->action->keep) {
			sieve_result_global_log(
				aenv, "store into mailbox %s aborted",
				mailbox_identifier);
		} else {
			e_debug(aenv->event, "Store into mailbox %s aborted",
				mailbox_identifier);
		}
	/* Succeeded */
	} else {
		struct event_passthrough *e =
			sieve_action_create_finish_event(aenv)->
			add_str("fileinto_mailbox_name", mailbox_name)->
			add_str("fileinto_mailbox", mailbox_identifier);
		sieve_result_event_log(aenv, e->event(),
				       "stored mail into mailbox %s",
				       mailbox_identifier);
	}
}

static void act_store_cleanup(struct act_store_transaction *trans)
{
	if (trans->mail_trans != NULL)
		mailbox_transaction_rollback(&trans->mail_trans);
	if (trans->box != NULL)
		mailbox_free(&trans->box);
}

static int
act_store_commit(const struct sieve_action_exec_env *aenv, void *tr_context)
{
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct act_store_transaction *trans =
		(struct act_store_transaction *)tr_context;
	bool bail_out = FALSE, status = TRUE;
	int ret = SIEVE_EXEC_OK;

	/* Verify transaction */
	if (trans == NULL)
		return SIEVE_EXEC_FAILURE;

	e_debug(aenv->event, "Commit storing into mailbox %s",
		trans->mailbox_identifier);

	/* Check whether we can commit this transaction */
	if (trans->error_code != MAIL_ERROR_NONE) {
		/* Transaction already failed */
		bail_out = TRUE;
		status = FALSE;
		if (trans->error_code == MAIL_ERROR_TEMP)
			ret = SIEVE_EXEC_TEMP_FAILURE;
		else
			ret = SIEVE_EXEC_FAILURE;
	/* Check whether we need to do anything */
	} else if (trans->disabled) {
		/* Nothing to do */
		bail_out = TRUE;
	} else if (trans->redundant) {
		/* This transaction is redundant */
		bail_out = TRUE;
		eenv->exec_status->keep_original = TRUE;
		eenv->exec_status->message_saved = TRUE;
	}
	if (bail_out) {
		act_store_log_status(trans, aenv, FALSE, status);
		act_store_cleanup(trans);
		return ret;
	}

	i_assert(trans->box != NULL);
	i_assert(trans->mail_trans != NULL);

	/* Mark attempt to use storage. Can only get here when all previous
	   actions succeeded.
	 */
	eenv->exec_status->last_storage = mailbox_get_storage(trans->box);

	/* Commit mailbox transaction */
	status = (mailbox_transaction_commit(&trans->mail_trans) == 0);

	/* Note the fact that the message was stored at least once */
	if (status)
		eenv->exec_status->message_saved = TRUE;
	else
		eenv->exec_status->store_failed = TRUE;

	/* Log our status */
	act_store_log_status(trans, aenv, FALSE, status);

	/* Clean up */
	act_store_cleanup(trans);

	if (status)
		return SIEVE_EXEC_OK;

	return (trans->error_code == MAIL_ERROR_TEMP ?
		SIEVE_EXEC_TEMP_FAILURE : SIEVE_EXEC_FAILURE);
}

static void
act_store_rollback(const struct sieve_action_exec_env *aenv, void *tr_context,
		   bool success)
{
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct act_store_transaction *trans =
		(struct act_store_transaction *)tr_context;

	if (trans == NULL)
		return;

	e_debug(aenv->event, "Roll back storing into mailbox %s",
		trans->mailbox_identifier);

	i_assert(trans->box != NULL);

	if (!success) {
		eenv->exec_status->last_storage =
			mailbox_get_storage(trans->box);
		eenv->exec_status->store_failed = TRUE;
	}

	/* Log status */
	act_store_log_status(trans, aenv, TRUE, success);

	/* Rollback mailbox transaction and clean up */
	act_store_cleanup(trans);
}

/*
 * Redirect action
 */

int sieve_act_redirect_add_to_result(const struct sieve_runtime_env *renv,
				     const char *name,
				     struct sieve_side_effects_list *seffects,
				     const struct smtp_address *to_address)
{
	const struct sieve_execute_env *eenv = renv->exec_env;
	struct sieve_instance *svinst = eenv->svinst;
	struct act_redirect_context *act;
	pool_t pool;

	pool = sieve_result_pool(renv->result);
	act = p_new(pool, struct act_redirect_context, 1);
	act->to_address = smtp_address_clone(pool, to_address);

	if (sieve_result_add_action(renv, NULL, name, &act_redirect, seffects,
				    act, svinst->set->max_redirects,
				    TRUE) < 0)
		return SIEVE_EXEC_FAILURE;

	return SIEVE_EXEC_OK;
}

/*
 * Action utility functions
 */

/* Rejecting the mail */

static int
sieve_action_do_reject_mail(const struct sieve_action_exec_env *aenv,
			    const struct smtp_address *recipient,
			    const char *reason)
{
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct sieve_instance *svinst = eenv->svinst;
	const struct sieve_script_env *senv = eenv->scriptenv;
	const struct sieve_message_data *msgdata = eenv->msgdata;
	const struct smtp_address *sender, *orig_recipient;
	struct istream *input;
	struct ostream *output;
	struct sieve_smtp_context *sctx;
	const char *new_msgid, *boundary, *error;
	string_t *hdr;
	int ret;

	sender = sieve_message_get_sender(aenv->msgctx);
	orig_recipient = msgdata->envelope.rcpt_params->orcpt.addr;

	sctx = sieve_smtp_start_single(senv, sender, NULL, &output);

	/* Just to be sure */
	if (sctx == NULL) {
		sieve_result_global_warning(
			aenv, "reject action has no means to send mail");
		return SIEVE_EXEC_OK;
	}

	new_msgid = sieve_message_get_new_id(svinst);
	boundary = t_strdup_printf("%s/%s", my_pid, svinst->hostname);

	hdr = t_str_new(512);
	rfc2822_header_write(hdr, "X-Sieve", SIEVE_IMPLEMENTATION);
	rfc2822_header_write(hdr, "Message-ID", new_msgid);
	rfc2822_header_write(hdr, "Date", message_date_create(ioloop_time));
	rfc2822_header_write(hdr, "From", sieve_get_postmaster_address(senv));
	rfc2822_header_printf(hdr, "To", "<%s>", smtp_address_encode(sender));
	rfc2822_header_write(hdr, "Subject", "Automatically rejected mail");
	rfc2822_header_write(hdr, "Auto-Submitted", "auto-replied (rejected)");
	rfc2822_header_write(hdr, "Precedence", "bulk");

	rfc2822_header_write(hdr, "MIME-Version", "1.0");
	rfc2822_header_printf(hdr, "Content-Type",
			      "multipart/report; "
			      "report-type=disposition-notification;\r\n"
			      "boundary=\"%s\"", boundary);

	str_append(hdr, "\r\nThis is a MIME-encapsulated message\r\n\r\n");

	/* Human readable status report */
	str_printfa(hdr, "--%s\r\n", boundary);
	rfc2822_header_write(hdr, "Content-Type", "text/plain; charset=utf-8");
	rfc2822_header_write(hdr, "Content-Disposition", "inline");
	rfc2822_header_write(hdr, "Content-Transfer-Encoding", "8bit");

	str_printfa(hdr, "\r\nYour message to <%s> was automatically rejected:\r\n"
		    "%s\r\n", smtp_address_encode(recipient), reason);

	/* MDN status report */
	str_printfa(hdr, "--%s\r\n", boundary);
	rfc2822_header_write(hdr, "Content-Type",
			     "message/disposition-notification");
	str_append(hdr, "\r\n");
	rfc2822_header_write(hdr,
			     "Reporting-UA: %s; Dovecot Mail Delivery Agent",
			     svinst->hostname);
	if (orig_recipient != NULL) {
		rfc2822_header_printf(hdr, "Original-Recipient", "rfc822; %s",
				      smtp_address_encode(orig_recipient));
	}
	rfc2822_header_printf(hdr, "Final-Recipient", "rfc822; %s",
			      smtp_address_encode(recipient));

	if (msgdata->id != NULL)
		rfc2822_header_write(hdr, "Original-Message-ID", msgdata->id);
	rfc2822_header_write(hdr, "Disposition",
		"automatic-action/MDN-sent-automatically; deleted");
	str_append(hdr, "\r\n");

	/* original message's headers */
	str_printfa(hdr, "--%s\r\n", boundary);
	rfc2822_header_write(hdr, "Content-Type", "message/rfc822");
	str_append(hdr, "\r\n");
	o_stream_nsend(output, str_data(hdr), str_len(hdr));

	if (mail_get_hdr_stream(msgdata->mail, NULL, &input) == 0) {
		/* NOTE: If you add more headers, they need to be sorted. We'll
		   drop Content-Type because we're not including the message
		   body, and having a multipart Content-Type may confuse some
		   MIME parsers when they don't see the message boundaries.
		 */
		static const char *const exclude_headers[] = {
			"Content-Type"
		};

		input = i_stream_create_header_filter(
			input, HEADER_FILTER_EXCLUDE | HEADER_FILTER_NO_CR |
			       HEADER_FILTER_HIDE_BODY,
			exclude_headers, N_ELEMENTS(exclude_headers),
			*null_header_filter_callback, NULL);
		o_stream_nsend_istream(output, input);
		i_stream_unref(&input);
	}

	str_truncate(hdr, 0);
	str_printfa(hdr, "\r\n\r\n--%s--\r\n", boundary);
	o_stream_nsend(output, str_data(hdr), str_len(hdr));

	if ((ret = sieve_smtp_finish(sctx, &error)) <= 0) {
		if (ret < 0) {
			sieve_result_global_error(aenv,
				"failed to send rejection message to <%s>: %s "
				"(temporary failure)",
				smtp_address_encode(sender),
				str_sanitize(error, 512));
		} else {
			sieve_result_global_log_error(aenv,
				"failed to send rejection message to <%s>: %s "
				"(permanent failure)",
				smtp_address_encode(sender),
				str_sanitize(error, 512));
		}
		return SIEVE_EXEC_FAILURE;
	}

	return SIEVE_EXEC_OK;
}

int sieve_action_reject_mail(const struct sieve_action_exec_env *aenv,
			     const struct smtp_address *recipient,
			     const char *reason)
{
	const struct sieve_execute_env *eenv = aenv->exec_env;
	const struct sieve_script_env *senv = eenv->scriptenv;
	int result;

	T_BEGIN {
		if (senv->reject_mail != NULL) {
			result = (senv->reject_mail(senv, recipient,
						    reason) >= 0 ?
				  SIEVE_EXEC_OK : SIEVE_EXEC_FAILURE);
		} else {
			result = sieve_action_do_reject_mail(aenv, recipient,
							     reason);
		}
	} T_END;

	return result;
}

/*
 * Mailbox
 */

bool sieve_mailbox_check_name(const char *mailbox, const char **error_r)
{
	if (!uni_utf8_str_is_valid(mailbox)) {
		*error_r = "invalid utf-8";
		return FALSE;
	}
	return TRUE;
}


