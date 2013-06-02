/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
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
#include "mail-deliver.h"
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

#include <ctype.h>

/*
 * Side-effect operand
 */

const struct sieve_operand_class sieve_side_effect_operand_class =
	{ "SIDE-EFFECT" };

bool sieve_opr_side_effect_read
(const struct sieve_runtime_env *renv, sieve_size_t *address,
	struct sieve_side_effect *seffect)
{
	const struct sieve_side_effect_def *sdef;

	seffect->context = NULL;

	if ( !sieve_opr_object_read
		(renv, &sieve_side_effect_operand_class, address, &seffect->object) )
		return FALSE;

	sdef = seffect->def =
		(const struct sieve_side_effect_def *) seffect->object.def;

	if ( sdef->read_context != NULL &&
		!sdef->read_context(seffect, renv, address, &seffect->context) ) {
		return FALSE;
	}

	return TRUE;
}

bool sieve_opr_side_effect_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	struct sieve_side_effect seffect;
	const struct sieve_side_effect_def *sdef;

	if ( !sieve_opr_object_dump
		(denv, &sieve_side_effect_operand_class, address, &seffect.object) )
		return FALSE;

	sdef = seffect.def =
		(const struct sieve_side_effect_def *) seffect.object.def;

	if ( sdef->dump_context != NULL ) {
		sieve_code_descend(denv);
		if ( !sdef->dump_context(&seffect, denv, address) ) {
			return FALSE;
		}
		sieve_code_ascend(denv);
	}

	return TRUE;
}

/*
 * Optional operands
 */

int sieve_action_opr_optional_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address,
	signed int *opt_code)
{
	signed int _opt_code = 0;
	bool final = FALSE, opok = TRUE;

	if ( opt_code == NULL ) {
		opt_code = &_opt_code;
		final = TRUE;
	}

	while ( opok ) {
		int opt;

		if ( (opt=sieve_opr_optional_dump(denv, address, opt_code)) <= 0 )
			return opt;

		if ( *opt_code == SIEVE_OPT_SIDE_EFFECT ) {
			opok = sieve_opr_side_effect_dump(denv, address);
		} else {
			return ( final ? -1 : 1 );
		}
	}

	return -1;
}

int sieve_action_opr_optional_read
(const struct sieve_runtime_env *renv, sieve_size_t *address,
	signed int *opt_code, int *exec_status,
	struct sieve_side_effects_list **list)
{
	signed int _opt_code = 0;
	bool final = FALSE;
	int ret;

	if ( opt_code == NULL ) {
		opt_code = &_opt_code;
		final = TRUE;
	}

	if ( exec_status != NULL )
		*exec_status = SIEVE_EXEC_OK;

	for ( ;; ) {
		int opt;

		if ( (opt=sieve_opr_optional_read(renv, address, opt_code)) <= 0 ) {
			if ( opt < 0 && exec_status != NULL )
				*exec_status = SIEVE_EXEC_BIN_CORRUPT;
			return opt;
		}

		if ( *opt_code == SIEVE_OPT_SIDE_EFFECT ) {
			struct sieve_side_effect seffect;

			i_assert( list != NULL );

			if ( (ret=sieve_opr_side_effect_read(renv, address, &seffect)) <= 0 ) {
				if ( exec_status != NULL )
					*exec_status = ret;
				return -1;
			}

			if ( *list == NULL )
				*list = sieve_side_effects_list_create(renv->result);

			sieve_side_effects_list_add(*list, &seffect);
		} else {
			if ( final ) {
				sieve_runtime_trace_error(renv, "invalid optional operand");
				if ( exec_status != NULL )
					*exec_status = SIEVE_EXEC_BIN_CORRUPT;
				return -1;
			}
			return 1;
		}
	}

	i_unreached();
	return -1;
}

/*
 * Store action
 */

/* Forward declarations */

static bool act_store_equals
	(const struct sieve_script_env *senv,
		const struct sieve_action *act1, const struct sieve_action *act2);

static int act_store_check_duplicate
	(const struct sieve_runtime_env *renv,
		const struct sieve_action *act,
		const struct sieve_action *act_other);
static void act_store_print
	(const struct sieve_action *action,
		const struct sieve_result_print_env *rpenv, bool *keep);

static int act_store_start
	(const struct sieve_action *action,
		const struct sieve_action_exec_env *aenv, void **tr_context);
static int act_store_execute
	(const struct sieve_action *action,
		const struct sieve_action_exec_env *aenv, void *tr_context);
static int act_store_commit
	(const struct sieve_action *action,
		const struct sieve_action_exec_env *aenv, void *tr_context, bool *keep);
static void act_store_rollback
	(const struct sieve_action *action,
		const struct sieve_action_exec_env *aenv, void *tr_context, bool success);

/* Action object */

const struct sieve_action_def act_store = {
	"store",
	SIEVE_ACTFLAG_TRIES_DELIVER,
	act_store_equals,
	act_store_check_duplicate,
	NULL,
	act_store_print,
	act_store_start,
	act_store_execute,
	act_store_commit,
	act_store_rollback,
};

/* API */

int sieve_act_store_add_to_result
(const struct sieve_runtime_env *renv,
	struct sieve_side_effects_list *seffects, const char *mailbox)
{
	pool_t pool;
	struct act_store_context *act;

	/* Add redirect action to the result */
	pool = sieve_result_pool(renv->result);
	act = p_new(pool, struct act_store_context, 1);
	act->mailbox = p_strdup(pool, mailbox);

	return sieve_result_add_action
		(renv, NULL, &act_store, seffects, (void *)act, 0, TRUE);
}

void sieve_act_store_add_flags
(const struct sieve_action_exec_env *aenv, void *tr_context,
	const char *const *keywords, enum mail_flags flags)
{
	struct act_store_transaction *trans =
		(struct act_store_transaction *) tr_context;

	/* Assign mail keywords for subsequent mailbox_copy() */
	if ( *keywords != NULL ) {
		const char *const *kw;

		if ( !array_is_created(&trans->keywords) ) {
			pool_t pool = sieve_result_pool(aenv->result);
			p_array_init(&trans->keywords, pool, 2);
		}

		kw = keywords;
		while ( *kw != NULL ) {

			const char *kw_error;

			if ( trans->box != NULL && trans->error_code == MAIL_ERROR_NONE ) {
				if ( mailbox_keyword_is_valid(trans->box, *kw, &kw_error) )
					array_append(&trans->keywords, kw, 1);
				else {
					char *error = "";
					if ( kw_error != NULL && *kw_error != '\0' ) {
						error = t_strdup_noconst(kw_error);
						error[0] = i_tolower(error[0]);
					}

					sieve_result_warning(aenv,
						"specified IMAP keyword '%s' is invalid (ignored): %s",
						str_sanitize(*kw, 64), error);
				}
			}

			kw++;
		}
	}

	/* Assign mail flags for subsequent mailbox_copy() */
	trans->flags |= flags;

	trans->flags_altered = TRUE;
}

/* Equality */

static bool act_store_equals
(const struct sieve_script_env *senv,
	const struct sieve_action *act1, const struct sieve_action *act2)
{
	struct act_store_context *st_ctx1 =
		( act1 == NULL ? NULL : (struct act_store_context *) act1->context );
	struct act_store_context *st_ctx2 =
		( act2 == NULL ? NULL : (struct act_store_context *) act2->context );
	const char *mailbox1, *mailbox2;

	/* FIXME: consider namespace aliases */

	if ( st_ctx1 == NULL && st_ctx2 == NULL )
		return TRUE;

	mailbox1 = ( st_ctx1 == NULL ?
		SIEVE_SCRIPT_DEFAULT_MAILBOX(senv) : st_ctx1->mailbox );
	mailbox2 = ( st_ctx2 == NULL ?
		SIEVE_SCRIPT_DEFAULT_MAILBOX(senv) : st_ctx2->mailbox );

	if ( strcmp(mailbox1, mailbox2) == 0 )
		return TRUE;

	return
		( strcasecmp(mailbox1, "INBOX") == 0 && strcasecmp(mailbox2, "INBOX") == 0 );

}

/* Result verification */

static int act_store_check_duplicate
(const struct sieve_runtime_env *renv,
	const struct sieve_action *act,
	const struct sieve_action *act_other)
{
	return ( act_store_equals(renv->scriptenv, act, act_other) ? 1 : 0 );
}

/* Result printing */

static void act_store_print
(const struct sieve_action *action,
	const struct sieve_result_print_env *rpenv, bool *keep)
{
	struct act_store_context *ctx = (struct act_store_context *) action->context;
	const char *mailbox;

	mailbox = ( ctx == NULL ?
		SIEVE_SCRIPT_DEFAULT_MAILBOX(rpenv->scriptenv) : ctx->mailbox );

	sieve_result_action_printf(rpenv, "store message in folder: %s",
		str_sanitize(mailbox, 128));

	*keep = FALSE;
}

/* Action implementation */

void sieve_act_store_get_storage_error
(const struct sieve_action_exec_env *aenv,
	struct act_store_transaction *trans)
{
	pool_t pool = sieve_result_pool(aenv->result);

	trans->error = p_strdup(pool,
		mail_storage_get_last_error(mailbox_get_storage(trans->box),
		&trans->error_code));
}

static bool act_store_mailbox_open
(const struct sieve_action_exec_env *aenv, const char *mailbox,
	struct mailbox **box_r, enum mail_error *error_code_r, const char **error_r)
{
	struct mail_storage **storage = &(aenv->exec_status->last_storage);
	struct mail_deliver_save_open_context save_ctx;

	*box_r = NULL;

	if ( !uni_utf8_str_is_valid(mailbox) ) {
		/* Just a precaution; already (supposed to be) checked at
		 * compiletime/runtime.
		 */
		*error_r = t_strdup_printf("mailbox name not utf-8: %s", mailbox);
		*error_code_r = MAIL_ERROR_PARAMS;
		return FALSE;
	}

	memset(&save_ctx, 0, sizeof(save_ctx));
	save_ctx.user = aenv->scriptenv->user;
	save_ctx.lda_mailbox_autocreate = aenv->scriptenv->mailbox_autocreate;
	save_ctx.lda_mailbox_autosubscribe = aenv->scriptenv->mailbox_autosubscribe;

	if ( mail_deliver_save_open
		(&save_ctx, mailbox, box_r, error_code_r, error_r) < 0 )
		return FALSE;

	*storage = mailbox_get_storage(*box_r);
	return TRUE;
}

static int act_store_start
(const struct sieve_action *action,
	const struct sieve_action_exec_env *aenv, void **tr_context)
{
	struct act_store_context *ctx = (struct act_store_context *) action->context;
	const struct sieve_script_env *senv = aenv->scriptenv;
	struct act_store_transaction *trans;
	struct mailbox *box = NULL;
	pool_t pool = sieve_result_pool(aenv->result);
	const char *error = NULL;
	enum mail_error error_code = MAIL_ERROR_NONE;
	bool disabled = FALSE, open_failed = FALSE;

	/* If context is NULL, the store action is the result of (implicit) keep */
	if ( ctx == NULL ) {
		ctx = p_new(pool, struct act_store_context, 1);
		ctx->mailbox = p_strdup(pool, SIEVE_SCRIPT_DEFAULT_MAILBOX(senv));
	}

	/* Open the requested mailbox */

	/* NOTE: The caller of the sieve library is allowed to leave user set
	 * to NULL. This implementation will then skip actually storing the message.
	 */
	if ( senv->user != NULL ) {
		if ( !act_store_mailbox_open(aenv, ctx->mailbox, &box, &error_code, &error) ) {
			open_failed = TRUE;
		}
	} else {
		disabled = TRUE;
	}

	/* Create transaction context */
	trans = p_new(pool, struct act_store_transaction, 1);

	trans->context = ctx;
	trans->box = box;
	trans->flags = 0;

	trans->disabled = disabled;

	if ( open_failed  ) {
		trans->error = error;
		trans->error_code = error_code;
	} else {
		trans->error_code = MAIL_ERROR_NONE;
	}

	*tr_context = (void *)trans;

	switch ( trans->error_code ) {
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

static struct mail_keywords *act_store_keywords_create
(const struct sieve_action_exec_env *aenv, ARRAY_TYPE(const_string) *keywords,
	struct mailbox *box)
{
	struct mail_keywords *box_keywords = NULL;

	if ( array_is_created(keywords) && array_count(keywords) > 0 )
	{
		const char *const *kwds;

		(void)array_append_space(keywords);
		kwds = array_idx(keywords, 0);

		/* FIXME: Do we need to clear duplicates? */
		if ( mailbox_keywords_create(box, kwds, &box_keywords) < 0) {
			sieve_result_error(aenv, "invalid keywords set for stored message");
			return NULL;
		}
	}

	return box_keywords;
}

static int act_store_execute
(const struct sieve_action *action,
	const struct sieve_action_exec_env *aenv, void *tr_context)
{
	struct act_store_transaction *trans =
		(struct act_store_transaction *) tr_context;
	struct mail *mail =	( action->mail != NULL ?
		action->mail : aenv->msgdata->mail );
	struct mail *real_mail = mail_get_real_mail(mail);
	struct mail_save_context *save_ctx;
	struct mail_keywords *keywords = NULL;
	int status = SIEVE_EXEC_OK;

	/* Verify transaction */
	if ( trans == NULL ) return SIEVE_EXEC_FAILURE;

	/* Check whether we need to do anything */
	if ( trans->disabled ) return SIEVE_EXEC_OK;

	/* Exit early if mailbox is not available */
	if ( trans->box == NULL )
		return SIEVE_EXEC_FAILURE;

	/* Exit early if transaction already failed */
 	switch ( trans->error_code ) {
	case MAIL_ERROR_NONE:
		break;
	case MAIL_ERROR_TEMP:
		return SIEVE_EXEC_TEMP_FAILURE;
	default:
		return SIEVE_EXEC_FAILURE;
	}

	/* If the message originates from the target mailbox, only update the flags
	 * and keywords (if not read-only)
	 */
	if ( mailbox_backends_equal(trans->box, mail->box) ||
		(real_mail != mail && mailbox_backends_equal(trans->box, real_mail->box)) )
		{
		trans->redundant = TRUE;

		if ( trans->flags_altered && !mailbox_is_readonly(mail->box) ) {
			keywords = act_store_keywords_create
				(aenv, &trans->keywords, mail->box);

			if ( keywords != NULL ) {
				mail_update_keywords(mail, MODIFY_REPLACE, keywords);
				mailbox_keywords_unref(&keywords);
			}

			mail_update_flags(mail, MODIFY_REPLACE, trans->flags);
		}

		return SIEVE_EXEC_OK;

	/* If the message is modified, only store it in the source mailbox when it is
	 * not opened read-only. Mail structs of modified messages have their own
	 * mailbox, unrelated to the orignal mail, so this case needs to be handled
	 * separately.
	 */
	} else if ( mail != aenv->msgdata->mail
		&& mailbox_is_readonly(aenv->msgdata->mail->box)
		&& ( mailbox_backends_equal(trans->box, aenv->msgdata->mail->box) ) ) {

		trans->redundant = TRUE;
		return SIEVE_EXEC_OK;
	}

	/* Mark attempt to store in default mailbox */
	if ( strcmp(trans->context->mailbox,
		SIEVE_SCRIPT_DEFAULT_MAILBOX(aenv->scriptenv)) == 0 )
		aenv->exec_status->tried_default_save = TRUE;

	/* Mark attempt to use storage. Can only get here when all previous actions
	 * succeeded.
	 */
	aenv->exec_status->last_storage = mailbox_get_storage(trans->box);

	/* Start mail transaction */
	trans->mail_trans = mailbox_transaction_begin
		(trans->box, MAILBOX_TRANSACTION_FLAG_EXTERNAL);

	/* Create mail object for stored message */
	trans->dest_mail = mail_alloc(trans->mail_trans, 0, NULL);

	/* Store the message */
	save_ctx = mailbox_save_alloc(trans->mail_trans);
	mailbox_save_set_dest_mail(save_ctx, trans->dest_mail);

	/* Apply keywords and flags that side-effects may have added */
	if ( trans->flags_altered ) {
		keywords = act_store_keywords_create(aenv, &trans->keywords, trans->box);

		mailbox_save_set_flags(save_ctx, trans->flags, keywords);
	} else {
		mailbox_save_copy_flags(save_ctx, mail);
	}

	if ( mailbox_copy(&save_ctx, mail) < 0 ) {
		sieve_act_store_get_storage_error(aenv, trans);
		status = ( trans->error_code == MAIL_ERROR_TEMP ?
			SIEVE_EXEC_TEMP_FAILURE : SIEVE_EXEC_FAILURE );
	}

	/* Deallocate keywords */
 	if ( keywords != NULL ) {
 		mailbox_keywords_unref(&keywords);
 	}

	return status;
}

static void act_store_log_status
(struct act_store_transaction *trans, const struct sieve_action_exec_env *aenv,
	bool rolled_back, bool status )
{
	const char *mailbox_name;

	mailbox_name = str_sanitize(trans->context->mailbox, 128);

	if ( trans->box != NULL ) {
		const char *mailbox_vname = mailbox_get_vname(trans->box);

		if ( strcmp(mailbox_name, mailbox_vname) != 0 )
			mailbox_name =
				t_strdup_printf("'%s' (%s)", mailbox_name, mailbox_vname);
		else
			mailbox_name = t_strdup_printf("'%s'", mailbox_name);
	} else {
		mailbox_name = t_strdup_printf("'%s'", mailbox_name);
	}

	/* Store disabled? */
	if ( trans->disabled ) {
		sieve_result_global_log
			(aenv, "store into mailbox %s skipped", mailbox_name);

	/* Store redundant? */
	} else if ( trans->redundant ) {
		sieve_result_global_log
			(aenv, "left message in mailbox %s", mailbox_name);

	/* Store failed? */
	} else if ( !status ) {
		const char *errstr;
		enum mail_error error_code;

		if ( trans->error == NULL )
			sieve_act_store_get_storage_error(aenv, trans);

		errstr = trans->error;
		error_code = trans->error_code;

		if ( error_code == MAIL_ERROR_NOTFOUND ||
			error_code == MAIL_ERROR_PARAMS ) {
			sieve_result_error(aenv, "failed to store into mailbox %s: %s",
				mailbox_name, errstr);
		} else if ( error_code == MAIL_ERROR_NOSPACE ) {
			sieve_result_global_log_error
				(aenv, "failed to store into mailbox %s: %s", mailbox_name, errstr);
		} else {
			sieve_result_global_error(aenv, "failed to store into mailbox %s: %s",
				mailbox_name, errstr);
		}

	/* Store aborted? */
	} else if ( rolled_back ) {
		sieve_result_global_log
			(aenv, "store into mailbox %s aborted", mailbox_name);

	/* Succeeded */
	} else {
		sieve_result_global_log
			(aenv, "stored mail into mailbox %s", mailbox_name);
	}
}

static int act_store_commit
(const struct sieve_action *action ATTR_UNUSED,
	const struct sieve_action_exec_env *aenv, void *tr_context, bool *keep)
{
	struct act_store_transaction *trans =
		(struct act_store_transaction *) tr_context;
	bool status = TRUE;

	/* Verify transaction */
	if ( trans == NULL ) return SIEVE_EXEC_FAILURE;

	/* Check whether we need to do anything */
	if ( trans->disabled ) {
		act_store_log_status(trans, aenv, FALSE, status);
		*keep = FALSE;
		if ( trans->box != NULL )
			mailbox_free(&trans->box);
		return SIEVE_EXEC_OK;
	} else if ( trans->redundant ) {
		act_store_log_status(trans, aenv, FALSE, status);
		aenv->exec_status->keep_original = TRUE;
		aenv->exec_status->message_saved = TRUE;
		if ( trans->box != NULL )
			mailbox_free(&trans->box);
		return SIEVE_EXEC_OK;
	}

	/* Mark attempt to use storage. Can only get here when all previous actions
	 * succeeded.
	 */
	aenv->exec_status->last_storage = mailbox_get_storage(trans->box);

	/* Free mail object for stored message */
	if ( trans->dest_mail != NULL )
		mail_free(&trans->dest_mail);

	/* Commit mailbox transaction */
	status = ( mailbox_transaction_commit(&trans->mail_trans) == 0 );

	/* Note the fact that the message was stored at least once */
	if ( status )
		aenv->exec_status->message_saved = TRUE;

	/* Log our status */
	act_store_log_status(trans, aenv, FALSE, status);

	/* Cancel implicit keep if all went well */
	*keep = !status;

	/* Close mailbox */
	if ( trans->box != NULL )
		mailbox_free(&trans->box);

	if (status)
		return SIEVE_EXEC_OK;

	return ( trans->error_code == MAIL_ERROR_TEMP ?
			SIEVE_EXEC_TEMP_FAILURE : SIEVE_EXEC_FAILURE );
}

static void act_store_rollback
(const struct sieve_action *action ATTR_UNUSED,
	const struct sieve_action_exec_env *aenv, void *tr_context, bool success)
{
	struct act_store_transaction *trans =
		(struct act_store_transaction *) tr_context;

	/* Log status */
	act_store_log_status(trans, aenv, TRUE, success);

	/* Free mailobject for stored message */
	if ( trans->dest_mail != NULL )
		mail_free(&trans->dest_mail);

	/* Rollback mailbox transaction */
	if ( trans->mail_trans != NULL )
		mailbox_transaction_rollback(&trans->mail_trans);

	/* Close the mailbox */
	if ( trans->box != NULL )
		mailbox_free(&trans->box);
}

/*
 * Action utility functions
 */

/* Checking for duplicates */

bool sieve_action_duplicate_check_available
(const struct sieve_script_env *senv)
{
	return ( senv->duplicate_check != NULL && senv->duplicate_mark != NULL );
}

int sieve_action_duplicate_check
(const struct sieve_script_env *senv, const void *id, size_t id_size)
{
	if ( senv->duplicate_check == NULL || senv->duplicate_mark == NULL)
		return 0;

	return senv->duplicate_check(senv, id, id_size);
}

void sieve_action_duplicate_mark
(const struct sieve_script_env *senv, const void *id, size_t id_size,
	time_t time)
{
	if ( senv->duplicate_check == NULL || senv->duplicate_mark == NULL)
		return;

	senv->duplicate_mark(senv, id, id_size, time);
}

/* Rejecting the mail */

static bool sieve_action_do_reject_mail
(const struct sieve_action_exec_env *aenv, const char *sender,
	const char *recipient, const char *reason)
{
	struct sieve_instance *svinst = aenv->svinst;
	const struct sieve_script_env *senv = aenv->scriptenv;
	const struct sieve_message_data *msgdata = aenv->msgdata;
	struct istream *input;
	struct ostream *output;
	void *smtp_handle;
	const char *new_msgid, *boundary;
	const char *header;
  string_t *hdr;
	int ret;

	/* Just to be sure */
	if ( !sieve_smtp_available(senv) ) {
		sieve_result_global_warning
			(aenv, "reject action has no means to send mail");
		return TRUE;
	}

	smtp_handle = sieve_smtp_open(senv, sender, NULL, &output);

	new_msgid = sieve_message_get_new_id(svinst);
	boundary = t_strdup_printf("%s/%s", my_pid, svinst->hostname);

  hdr = t_str_new(512);
	rfc2822_header_write(hdr, "X-Sieve", SIEVE_IMPLEMENTATION);
	rfc2822_header_write(hdr, "Message-ID", new_msgid);
	rfc2822_header_write(hdr, "Date", message_date_create(ioloop_time));
	rfc2822_header_printf(hdr, "From", "Mail Delivery Subsystem <%s>",
		senv->postmaster_address);
	rfc2822_header_printf(hdr, "To", "<%s>", sender);
	rfc2822_header_write(hdr, "Subject", "Automatically rejected mail");
	rfc2822_header_write(hdr, "Auto-Submitted", "auto-replied (rejected)");
	rfc2822_header_write(hdr, "Precedence", "bulk");

	rfc2822_header_write(hdr, "MIME-Version", "1.0");
	rfc2822_header_printf(hdr, "Content-Type",
		"multipart/report; report-type=disposition-notification;\r\n"
		"boundary=\"%s\"", boundary);

	str_append(hdr, "\r\nThis is a MIME-encapsulated message\r\n\r\n");

	/* Human readable status report */
	str_printfa(hdr, "--%s\r\n", boundary);
	rfc2822_header_write(hdr, "Content-Type", "text/plain; charset=utf-8");
	rfc2822_header_write(hdr, "Content-Disposition", "inline");
	rfc2822_header_write(hdr, "Content-Transfer-Encoding", "8bit");

	str_printfa(hdr, "\r\nYour message to <%s> was automatically rejected:\r\n"
		"%s\r\n", recipient, reason);

	/* MDN status report */
	str_printfa(hdr, "--%s\r\n", boundary);
	rfc2822_header_write(hdr, "Content-Type", "message/disposition-notification");
	str_append(hdr, "\r\n");
	rfc2822_header_write(hdr, "Reporting-UA: %s; Dovecot Mail Delivery Agent",
		svinst->hostname);
	if (mail_get_first_header(msgdata->mail, "Original-Recipient", &header) > 0)
		rfc2822_header_printf(hdr, "Original-Recipient", "rfc822; %s", header);
	rfc2822_header_printf(hdr, "Final-Recipient", "rfc822; %s", recipient);

	if ( msgdata->id != NULL )
		rfc2822_header_write(hdr, "Original-Message-ID", msgdata->id);
	rfc2822_header_write(hdr, "Disposition",
		"automatic-action/MDN-sent-automatically; deleted");
	str_append(hdr, "\r\n");

	/* original message's headers */
	str_printfa(hdr, "--%s\r\n", boundary);
	rfc2822_header_write(hdr, "Content-Type", "message/rfc822");
	str_append(hdr, "\r\n");
	o_stream_send(output, str_data(hdr), str_len(hdr));

	if (mail_get_hdr_stream(msgdata->mail, NULL, &input) == 0) {
    /* Note: If you add more headers, they need to be sorted.
       We'll drop Content-Type because we're not including the message
       body, and having a multipart Content-Type may confuse some
       MIME parsers when they don't see the message boundaries. */
    static const char *const exclude_headers[] = {
	    "Content-Type"
    };

    input = i_stream_create_header_filter(input,
    		HEADER_FILTER_EXCLUDE | HEADER_FILTER_NO_CR |
		HEADER_FILTER_HIDE_BODY, exclude_headers,
		N_ELEMENTS(exclude_headers),
		*null_header_filter_callback, (void *)NULL);

    ret = o_stream_send_istream(output, input);
    i_stream_unref(&input);

    i_assert(ret != 0);
  }

  str_truncate(hdr, 0);
  str_printfa(hdr, "\r\n\r\n--%s--\r\n", boundary);
  o_stream_send(output, str_data(hdr), str_len(hdr));

	if ( !sieve_smtp_close(senv, smtp_handle) ) {
		sieve_result_global_error(aenv,
			"failed to send rejection message to <%s> "
			"(refer to server log for more information)",
			str_sanitize(sender, 80));
		return FALSE;
	}

	return TRUE;
}

bool sieve_action_reject_mail
(const struct sieve_action_exec_env *aenv,
	const char *sender, const char *recipient, const char *reason)
{
	const struct sieve_script_env *senv = aenv->scriptenv;
	bool result;

	T_BEGIN {
		if ( senv->reject_mail != NULL ) {
			result =
				( senv->reject_mail(senv, recipient, reason) >= 0 );
		} else {
			result = sieve_action_do_reject_mail(aenv, sender, recipient, reason);
		}
	} T_END;

	return result;
}




