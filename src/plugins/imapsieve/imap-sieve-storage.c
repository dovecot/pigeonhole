/* Copyright (c) 2016 Dovecot authors, see the included COPYING file */

#include "imap-common.h"
#include "array.h"
#include "hash.h"
#include "str.h"
#include "istream.h"
#include "ostream.h"
#include "module-context.h"
#include "mail-user.h"
#include "mail-storage-private.h"
#include "mailbox-attribute.h"
#include "mailbox-list-private.h"
#include "imap-match.h"
#include "imap-util.h"

#include "strtrim.h"

#include "imap-sieve.h"
#include "imap-sieve-storage.h"

#define MAILBOX_ATTRIBUTE_IMAPSIEVE_SCRIPT "imapsieve/script"
#define MAIL_SERVER_ATTRIBUTE_IMAPSIEVE_SCRIPT "imapsieve/script"

#define IMAP_SIEVE_USER_CONTEXT(obj) \
	MODULE_CONTEXT(obj, imap_sieve_user_module)
#define IMAP_SIEVE_CONTEXT(obj) \
	MODULE_CONTEXT(obj, imap_sieve_storage_module)
#define IMAP_SIEVE_MAIL_CONTEXT(obj) \
	MODULE_CONTEXT(obj, imap_sieve_mail_module)

struct imap_sieve_mailbox_rule;
struct imap_sieve_user;
struct imap_sieve_mailbox_event;
struct imap_sieve_mailbox_transaction;
struct imap_sieve_mail;

enum imap_sieve_command {
	IMAP_SIEVE_CMD_NONE = 0,
	IMAP_SIEVE_CMD_APPEND,
	IMAP_SIEVE_CMD_COPY,
	IMAP_SIEVE_CMD_MOVE,
	IMAP_SIEVE_CMD_STORE,
	IMAP_SIEVE_CMD_OTHER
};

ARRAY_DEFINE_TYPE(imap_sieve_mailbox_rule,
	struct imap_sieve_mailbox_rule *);
ARRAY_DEFINE_TYPE(imap_sieve_mailbox_event,
	struct imap_sieve_mailbox_event);

HASH_TABLE_DEFINE_TYPE(imap_sieve_mailbox_rule,
	struct imap_sieve_mailbox_rule *,
	struct imap_sieve_mailbox_rule *);

struct imap_sieve_mailbox_rule {
	unsigned int index;
	const char *mailbox;
	const char *from;
	const char *const *causes;
	const char *before, *after;
};

struct imap_sieve_user {
	union mail_user_module_context module_ctx;
	struct client *client;
	struct imap_sieve *isieve;

	enum imap_sieve_command cur_cmd;

	HASH_TABLE_TYPE(imap_sieve_mailbox_rule) mbox_rules;
	ARRAY_TYPE(imap_sieve_mailbox_rule) mbox_patterns;

	unsigned int sieve_active:1;
	unsigned int user_script:1;
};

struct imap_sieve_mailbox_event {
	uint32_t mail_uid;
	unsigned int save_seq;

	const char *changed_flags;
};

struct imap_sieve_mailbox_transaction {
	pool_t pool;

	union mailbox_transaction_module_context module_ctx;
	struct mail *tmp_mail;
	struct mailbox *src_box;

	ARRAY_TYPE(imap_sieve_mailbox_event) events;
};

struct imap_sieve_mail {
	union mail_module_context module_ctx;

	string_t *flags;
};

static MODULE_CONTEXT_DEFINE_INIT(imap_sieve_user_module,
				  &mail_user_module_register);
static MODULE_CONTEXT_DEFINE_INIT(imap_sieve_storage_module,
				  &mail_storage_module_register);
static MODULE_CONTEXT_DEFINE_INIT(imap_sieve_mail_module,
				  &mail_module_register);

static void
imap_sieve_mailbox_rules_get(struct mail_user *user,
	struct mailbox *dst_box, struct mailbox *src_box,
	const char *cause,
	ARRAY_TYPE(imap_sieve_mailbox_rule) *rules);

/*
 * Logging
 */

static inline void
imap_sieve_debug(struct mail_user *user,
	const char *format, ...) ATTR_FORMAT(2, 3);
static inline void
imap_sieve_debug(struct mail_user *user,
	const char *format, ...)
{
	va_list args;

	if (user->mail_debug) {
		va_start(args, format);
		i_debug("imapsieve: %s",
			t_strdup_vprintf(format, args));
		va_end(args);
	}
}

static inline void
imap_sieve_warning(struct mail_user *user ATTR_UNUSED,
	const char *format, ...) ATTR_FORMAT(2, 3);
static inline void
imap_sieve_warning(struct mail_user *user ATTR_UNUSED,
	const char *format, ...)
{
	va_list args;

	va_start(args, format);
	i_warning("imapsieve: %s",
		t_strdup_vprintf(format, args));
	va_end(args);
}

static inline void
imap_sieve_mailbox_debug(struct mailbox *box,
	const char *format, ...) ATTR_FORMAT(2, 3);
static inline void
imap_sieve_mailbox_debug(struct mailbox *box,
	const char *format, ...)
{
	va_list args;

	if (box->storage->user->mail_debug) {
		va_start(args, format);
		i_debug("imapsieve: mailbox %s: %s",
			mailbox_get_vname(box),
			t_strdup_vprintf(format, args));
		va_end(args);
	}
}

static inline void
imap_sieve_mailbox_error(struct mailbox *box,
	const char *format, ...) ATTR_FORMAT(2, 3);
static inline void
imap_sieve_mailbox_error(struct mailbox *box,
	const char *format, ...)
{
	va_list args;

	va_start(args, format);
	i_error("imapsieve: mailbox %s: %s",
		mailbox_get_vname(box),
		t_strdup_vprintf(format, args));
	va_end(args);
}

/*
 * Events
 */

static int imap_sieve_mailbox_get_script_real
(struct mailbox *box, struct mailbox_transaction_context *t,
	const char **script_name_r)
{
	struct mail_user *user = box->storage->user;
	struct mail_attribute_value value;
	int ret;

	*script_name_r = NULL;

	/* get the name of the Sieve script from mailbox METADATA */
	if ((ret=mailbox_attribute_get(t, MAIL_ATTRIBUTE_TYPE_SHARED,
			MAILBOX_ATTRIBUTE_IMAPSIEVE_SCRIPT, &value)) < 0) {
		imap_sieve_mailbox_error(t->box,
			"Failed to read /shared/"
			MAILBOX_ATTRIBUTE_IMAPSIEVE_SCRIPT" "
			"mailbox attribute"); // FIXME: details?
		return -1;
	}

	if (ret > 0) {
		imap_sieve_mailbox_debug(t->box,
			"Mailbox attribute /shared/"
			MAILBOX_ATTRIBUTE_IMAPSIEVE_SCRIPT" "
			"points to Sieve script `%s'", value.value);

	/* if not found, get the name of the Sieve script from
	   server METADATA */
	} else {
		struct mail_namespace *ns;
		struct mailbox *box;
		struct mailbox_transaction_context *ibt;

		imap_sieve_mailbox_debug(t->box,
			"Mailbox attribute /shared/"
			MAILBOX_ATTRIBUTE_IMAPSIEVE_SCRIPT" "
			"not found");

		ns = mail_namespace_find_inbox(user->namespaces);
		box = mailbox_alloc(ns->list, "INBOX",
			MAILBOX_FLAG_READONLY);
		if ((ret=mailbox_open(box)) >= 0) {
			ibt = mailbox_transaction_begin
				(box, MAILBOX_TRANSACTION_FLAG_EXTERNAL);
			ret = mailbox_attribute_get(ibt,
				MAIL_ATTRIBUTE_TYPE_SHARED,
				MAILBOX_ATTRIBUTE_PREFIX_DOVECOT_PVT_SERVER
				MAILBOX_ATTRIBUTE_IMAPSIEVE_SCRIPT, &value);
			mailbox_transaction_rollback(&ibt);
		}
		mailbox_free(&box);

		if (ret <= 0) {
			if (ret < 0) {
				imap_sieve_mailbox_error(t->box,
					"Failed to read /shared/"
					MAIL_SERVER_ATTRIBUTE_IMAPSIEVE_SCRIPT" "
					"server attribute"); // FIXME: details?
			} else if (ret == 0) {
				imap_sieve_mailbox_debug(t->box,
					"Server attribute /shared/"
					MAIL_SERVER_ATTRIBUTE_IMAPSIEVE_SCRIPT" "
					"not found");
			}
			return ret;
		}

		imap_sieve_mailbox_debug(t->box,
			"Server attribute /shared/"
			MAIL_SERVER_ATTRIBUTE_IMAPSIEVE_SCRIPT" "
			"points to Sieve script `%s'", value.value);
	}

	*script_name_r = value.value;
	return 1;
}

static int imap_sieve_mailbox_get_script
(struct mailbox *box, const char **script_name_r)
{
	struct mailbox_transaction_context *t;
	int ret;

	t = mailbox_transaction_begin(box, 0);
	ret = imap_sieve_mailbox_get_script_real
		(box, t, script_name_r);
	mailbox_transaction_rollback(&t);
	return ret;
}

static void imap_sieve_add_mailbox_event
(struct mailbox_transaction_context *t,
	struct mail *mail, struct mailbox *src_box,
	const char *changed_flags)
{
	struct imap_sieve_mailbox_transaction *ismt = IMAP_SIEVE_CONTEXT(t);
	struct imap_sieve_mailbox_event *event;

	i_assert(ismt->src_box == NULL || ismt->src_box == src_box);
	ismt->src_box = src_box;

	if (!array_is_created(&ismt->events))
		i_array_init(&ismt->events, 64);

	event = array_append_space(&ismt->events);
	event->save_seq = t->save_count;
	event->mail_uid = mail->uid;
	event->changed_flags = p_strdup(ismt->pool, changed_flags);
}

/*
 * Mail
 */

static void
imap_sieve_mail_update_flags(struct mail *_mail,
	enum modify_type modify_type, enum mail_flags flags)
{
	struct mail_private *mail = (struct mail_private *)_mail;
	struct imap_sieve_mail *ismail = IMAP_SIEVE_MAIL_CONTEXT(mail);
	enum mail_flags old_flags, new_flags, changed_flags;

	old_flags = mail_get_flags(_mail);
	ismail->module_ctx.super.update_flags(_mail, modify_type, flags);
	new_flags = mail_get_flags(_mail);

	changed_flags = old_flags ^ new_flags;
	if (changed_flags == 0)
		return;

	if (ismail->flags == NULL)
		ismail->flags = str_new(default_pool, 64);
	imap_write_flags(ismail->flags, changed_flags, NULL);
}

static void
imap_sieve_mail_update_keywords(struct mail *_mail,
	enum modify_type modify_type, struct mail_keywords *keywords)
{
	struct mail_private *mail = (struct mail_private *)_mail;
	struct mail_user *user = _mail->box->storage->user;
	struct imap_sieve_mail *ismail = IMAP_SIEVE_MAIL_CONTEXT(mail);
	const char *const *old_keywords, *const *new_keywords;
	unsigned int i, j;

	old_keywords = mail_get_keywords(_mail);
	ismail->module_ctx.super.update_keywords(_mail, modify_type, keywords);
	new_keywords = mail_get_keywords(_mail);

	if (ismail->flags == NULL)
		ismail->flags = str_new(default_pool, 64);

	imap_sieve_debug(user, "Mail set keywords");

	/* Removed flags */
	for (i = 0; old_keywords[i] != NULL; i++) {
		for (j = 0; new_keywords[j] != NULL; j++) {
			if (strcmp(old_keywords[i], new_keywords[j]) == 0)
				break;
		}
		if (new_keywords[j] == NULL) {
			if (str_len(ismail->flags) > 0)
				str_append_c(ismail->flags, ' ');
			str_append(ismail->flags, old_keywords[i]);
		}
	}

	/* Added flags */
	for (i = 0; new_keywords[i] != NULL; i++) {
		for (j = 0; old_keywords[j] != NULL; j++) {
			if (strcmp(new_keywords[i], old_keywords[j]) == 0)
				break;
		}
		if (old_keywords[j] == NULL) {
			if (str_len(ismail->flags) > 0)
				str_append_c(ismail->flags, ' ');
			str_append(ismail->flags, new_keywords[i]);
		}
	}
}

static void imap_sieve_mail_close(struct mail *_mail)
{
	struct mail_private *mail = (struct mail_private *)_mail;
	struct mailbox_transaction_context *t = _mail->transaction;
	struct imap_sieve_mail *ismail = IMAP_SIEVE_MAIL_CONTEXT(mail);

	if (ismail->flags != NULL && str_len(ismail->flags) > 0) {
		if (!_mail->expunged) {
			imap_sieve_mailbox_debug(_mail->box,
				"FLAG event (changed flags: %s)",
				str_c(ismail->flags));

			imap_sieve_add_mailbox_event(t,
				_mail, _mail->box, str_c(ismail->flags));
		}
		str_truncate(ismail->flags, 0);
	}

	ismail->module_ctx.super.close(_mail);
}

static void imap_sieve_mail_free(struct mail *_mail)
{
	struct mail_private *mail = (struct mail_private *)_mail;
	struct imap_sieve_mail *ismail = IMAP_SIEVE_MAIL_CONTEXT(mail);
	string_t *flags = ismail->flags;

	ismail->module_ctx.super.free(_mail);

	if (flags != NULL)
		str_free(&flags);
}

static void imap_sieve_mail_allocated(struct mail *_mail)
{
	struct mail_private *mail = (struct mail_private *)_mail;
	struct imap_sieve_mailbox_transaction *ismt =
		IMAP_SIEVE_CONTEXT(_mail->transaction);
	struct mail_vfuncs *v = mail->vlast;
	struct imap_sieve_mail *ismail;

	if (ismt == NULL)
		return;

	ismail = p_new(mail->pool, struct imap_sieve_mail, 1);
	ismail->module_ctx.super = *v;
	mail->vlast = &ismail->module_ctx.super;

	v->close = imap_sieve_mail_close;
	v->free = imap_sieve_mail_free;
	v->update_flags = imap_sieve_mail_update_flags;
	v->update_keywords = imap_sieve_mail_update_keywords;
	MODULE_CONTEXT_SET(mail, imap_sieve_mail_module, ismail);
}

/*
 * Save/copy
 */

static int
imap_sieve_mailbox_copy(struct mail_save_context *ctx, struct mail *mail)
{
	struct mailbox_transaction_context *t = ctx->transaction;
	struct mail_storage *storage = t->box->storage;
	struct mail_user *user = storage->user;
	struct imap_sieve_user *isuser =
		IMAP_SIEVE_USER_CONTEXT(user);
	union mailbox_module_context *lbox =
		IMAP_SIEVE_CONTEXT(t->box);
	struct imap_sieve_mailbox_transaction *ismt =
		IMAP_SIEVE_CONTEXT(t);

	if (ismt != NULL) {
		if (ctx->dest_mail == NULL) {
			/* Dest mail is required for our purposes */
			if (ismt->tmp_mail == NULL) {
				ismt->tmp_mail = mail_alloc(t,
					MAIL_FETCH_STREAM_HEADER |
				  MAIL_FETCH_STREAM_BODY, NULL);
			}
			ctx->dest_mail = ismt->tmp_mail;
		}
	}

	if (lbox->super.copy(ctx, mail) < 0)
		return -1;

	if (ismt != NULL && !ctx->dest_mail->expunged &&
		(isuser->cur_cmd == IMAP_SIEVE_CMD_COPY ||
			isuser->cur_cmd == IMAP_SIEVE_CMD_MOVE)) {
		imap_sieve_mailbox_debug(t->box, "%s event",
			(isuser->cur_cmd == IMAP_SIEVE_CMD_COPY ?
				"COPY" : "MOVE"));
		imap_sieve_add_mailbox_event
			(t, ctx->dest_mail, mail->box, NULL);
	}

	return 0;
}

static int
imap_sieve_mailbox_save_begin(struct mail_save_context *ctx,
	struct istream *input)
{
	struct imap_sieve_mailbox_transaction *ismt =
		IMAP_SIEVE_CONTEXT(ctx->transaction);
	union mailbox_module_context *lbox =
		IMAP_SIEVE_CONTEXT(ctx->transaction->box);

	if (ismt != NULL) {
		if (ctx->dest_mail == NULL) {
			/* Dest mail is required for our purposes */
			if (ismt->tmp_mail == NULL) {
				ismt->tmp_mail = mail_alloc(ctx->transaction,
					MAIL_FETCH_STREAM_HEADER |
				  MAIL_FETCH_STREAM_BODY, NULL);
			}
			ctx->dest_mail = ismt->tmp_mail;
		}
	}
	return lbox->super.save_begin(ctx, input);
}

static int
imap_sieve_mailbox_save_finish(struct mail_save_context *ctx)
{
	struct mailbox_transaction_context *t = ctx->transaction;
	struct mailbox *box = t->box;
	struct imap_sieve_mailbox_transaction *ismt = IMAP_SIEVE_CONTEXT(t);
	union mailbox_module_context *lbox = IMAP_SIEVE_CONTEXT(box);
	struct mail_user *user = box->storage->user;
	struct imap_sieve_user *isuser = 	IMAP_SIEVE_USER_CONTEXT(user);
	struct mail *dest_mail = ctx->copying_via_save ? NULL : ctx->dest_mail;

	if (lbox->super.save_finish(ctx) < 0)
		return -1;

	if (ismt != NULL && dest_mail != NULL &&
		!dest_mail->expunged &&
		isuser->cur_cmd == IMAP_SIEVE_CMD_APPEND) {

		imap_sieve_mailbox_debug(t->box, "APPEND event");
		imap_sieve_add_mailbox_event(t, dest_mail, box, NULL);
	}
	return 0;
}

/*
 * Mailbox
 */

static struct mailbox_transaction_context *
imap_sieve_mailbox_transaction_begin(struct mailbox *box,
			 enum mailbox_transaction_flags flags)
{
	union mailbox_module_context *lbox = IMAP_SIEVE_CONTEXT(box);
	struct mail_user *user = box->storage->user;
	struct imap_sieve_user *isuser = 	IMAP_SIEVE_USER_CONTEXT(user);
	struct mailbox_transaction_context *t;
	struct imap_sieve_mailbox_transaction *ismt;
	pool_t pool;

	/* commence parent transaction */
	t = lbox->super.transaction_begin(box, flags);

	if (isuser == NULL || isuser->sieve_active)
		return t;

	i_assert(isuser->client != NULL);

	pool = pool_alloconly_create("imap_sieve_mailbox_transaction", 1024);
	ismt = p_new(pool, struct imap_sieve_mailbox_transaction, 1);
	ismt->pool = pool;
	MODULE_CONTEXT_SET(t, imap_sieve_storage_module, ismt);

	return t;
}

static void
imap_sieve_mailbox_transaction_free
(struct imap_sieve_mailbox_transaction *ismt)
{
	i_assert(ismt->tmp_mail == NULL);
	if (array_is_created(&ismt->events))
		array_free(&ismt->events);
	pool_unref(&ismt->pool);
}

static int
imap_sieve_mailbox_transaction_run(
	struct imap_sieve_mailbox_transaction *ismt,
	struct mailbox *box,
	struct mail_transaction_commit_changes *changes)
{
	static const char *wanted_headers[] = {
		"From", "To", "Message-ID", "Subject", "Return-Path",
		NULL
	};
	struct mailbox *src_box = ismt->src_box;
	struct mail_user *user = box->storage->user;
	struct imap_sieve_user *isuser = 	IMAP_SIEVE_USER_CONTEXT(user);
	const struct imap_sieve_mailbox_event *mevent;
	struct mailbox_header_lookup_ctx *headers_ctx;
	struct mailbox_transaction_context *st;
	struct mailbox *sbox;
	struct imap_sieve_run *isrun;
	struct seq_range_iter siter;
	const char *cause, *script_name = NULL;
	bool can_discard;
	struct mail *mail;
	int ret;

	if (ismt == NULL || !array_is_created(&ismt->events)) {
		/* Nothing to do */
		return 0;
	}

	i_assert(isuser->client != NULL);

	/* Get user script for this mailbox */
	if (isuser->user_script && imap_sieve_mailbox_get_script
		(box, &script_name) < 0) {
		return 0; // FIXME: some errors may warrant -1
	}

	/* Make sure IMAPSIEVE is initialized for this user */
	if (isuser->isieve == NULL) {
		isuser->isieve = imap_sieve_init
			(user, isuser->client->lda_set);
	}

	can_discard = FALSE;
	switch (isuser->cur_cmd) {
	case IMAP_SIEVE_CMD_APPEND:
		cause = "APPEND";
		can_discard = TRUE;
		break;
	case IMAP_SIEVE_CMD_COPY:
	case IMAP_SIEVE_CMD_MOVE:
		cause = "COPY";
		can_discard = TRUE;
		break;
	case IMAP_SIEVE_CMD_STORE:
	case IMAP_SIEVE_CMD_OTHER:
		cause = "FLAG";
		break;
	default:
		i_unreached();
	}

	/* Initialize execution */
	T_BEGIN {
		ARRAY_TYPE(imap_sieve_mailbox_rule) mbrules;
		ARRAY_TYPE(const_string) scripts_before, scripts_after;
		struct imap_sieve_mailbox_rule *const *rule_idx;

		/* Find matching rules */
		t_array_init(&mbrules, 16);
		imap_sieve_mailbox_rules_get
			(user, box, src_box, cause, &mbrules);

		/* Apply all matched rules */
		t_array_init(&scripts_before, 8);
		t_array_init(&scripts_after, 8);
		array_foreach(&mbrules, rule_idx) {
			struct imap_sieve_mailbox_rule *rule = *rule_idx;

			if (rule->before != NULL)
				array_append(&scripts_before, &rule->before, 1);
			if (rule->after != NULL)
				array_append(&scripts_after, &rule->after, 1);
		}
		(void)array_append_space(&scripts_before);
		(void)array_append_space(&scripts_after);

		/* Initialize */
		ret = imap_sieve_run_init
			(isuser->isieve, box, cause, script_name,
				array_idx(&scripts_before, 0),
				array_idx(&scripts_after, 0), &isrun);
	} T_END;

	if (ret <= 0) {
		// FIXME: temp fail should be handled properly
		return 0;
	}

	/* Get synchronized view on the mailbox */
	sbox = mailbox_alloc(box->list, box->vname, 0);
	if (mailbox_sync(sbox, 0) < 0) {
		mailbox_free(&sbox);
		return -1;
	}

	/* Create transaction for event messages */
	st = mailbox_transaction_begin(sbox, 0);
	headers_ctx = mailbox_header_lookup_init(sbox, wanted_headers);
	mail = mail_alloc(st, 0, headers_ctx);
	mailbox_header_lookup_unref(&headers_ctx);

	/* Iterate through all events */
	seq_range_array_iter_init(&siter, &changes->saved_uids);
	array_foreach(&ismt->events, mevent) {
		uint32_t uid;

		/* Determine UID for saved message */
		if (mevent->mail_uid > 0 ||
			!seq_range_array_iter_nth(&siter, mevent->save_seq, &uid))
			uid = mevent->mail_uid;

		/* Select event message */
		if (!mail_set_uid(mail, uid)) {
			imap_sieve_mailbox_error(sbox,
				"Failed to find message for Sieve event (UID=%llu)",
				(unsigned long long)uid);
			continue;
		}

		i_assert(!mail->expunged);

		/* Run scripts for this mail */
		ret = imap_sieve_run_mail
			(isrun, mail, mevent->changed_flags);

		/* Handle the result */
		if (ret < 0) {
			/* Sieve error; keep */
		} else if (ret > 0 && can_discard) {
			/* Discard */
			mail_update_flags(mail, MODIFY_ADD, MAIL_DELETED);
		}
	}

	/* Cleanup */
	mail_free(&mail);
	ret = mailbox_transaction_commit(&st);
	imap_sieve_run_deinit(&isrun);
	mailbox_free(&sbox);
	return ret;
}

static int
imap_sieve_mailbox_transaction_commit(
	struct mailbox_transaction_context *t,
	struct mail_transaction_commit_changes *changes_r)
{
	struct mailbox *box = t->box;
	struct mail_user *user = box->storage->user;
	struct imap_sieve_mailbox_transaction *ismt = IMAP_SIEVE_CONTEXT(t);
	union mailbox_module_context *lbox = IMAP_SIEVE_CONTEXT(t->box);
	struct imap_sieve_user *isuser = 	IMAP_SIEVE_USER_CONTEXT(user);
	int ret = 0;

	if (ismt != NULL && ismt->tmp_mail != NULL)
		mail_free(&ismt->tmp_mail);

	if ((lbox->super.transaction_commit(t, changes_r)) < 0)
		ret = -1;
	else if (ismt != NULL) {
		isuser->sieve_active = TRUE;
		if (imap_sieve_mailbox_transaction_run
			(ismt, box, changes_r) < 0)
			ret = -1;
		isuser->sieve_active = FALSE;
	}

	if (ismt != NULL)
		imap_sieve_mailbox_transaction_free(ismt);
	return ret;
}

static void
imap_sieve_mailbox_transaction_rollback(
	struct mailbox_transaction_context *t)
{
	struct imap_sieve_mailbox_transaction *ismt = IMAP_SIEVE_CONTEXT(t);
	union mailbox_module_context *lbox = IMAP_SIEVE_CONTEXT(t->box);

	if (ismt != NULL && ismt->tmp_mail != NULL)
		mail_free(&ismt->tmp_mail);

	lbox->super.transaction_rollback(t);

	if (ismt != NULL)
		imap_sieve_mailbox_transaction_free(ismt);
}

static void imap_sieve_mailbox_allocated(struct mailbox *box)
{
	struct mail_user *user = box->storage->user;
	struct imap_sieve_user *isuser = 	IMAP_SIEVE_USER_CONTEXT(user);
	struct mailbox_vfuncs *v = box->vlast;
	union mailbox_module_context *lbox;

	if (isuser->client == NULL || isuser->sieve_active ||
		(box->flags & MAILBOX_FLAG_READONLY) != 0)
		return;

	lbox = p_new(box->pool, union mailbox_module_context, 1);
	lbox->super = *v;
	box->vlast = &lbox->super;

	v->copy = imap_sieve_mailbox_copy;
	v->save_begin = imap_sieve_mailbox_save_begin;
	v->save_finish = imap_sieve_mailbox_save_finish;
	v->transaction_begin = imap_sieve_mailbox_transaction_begin;
	v->transaction_commit = imap_sieve_mailbox_transaction_commit;
	v->transaction_rollback = imap_sieve_mailbox_transaction_rollback;
	MODULE_CONTEXT_SET_SELF(box, imap_sieve_storage_module, lbox);
}

/*
 * Mailbox rules
 */

static unsigned int imap_sieve_mailbox_rule_hash
(const struct imap_sieve_mailbox_rule *rule)
{
	unsigned int hash = str_hash(rule->mailbox);

	if (rule->from != NULL)
		hash += str_hash(rule->from);
	return hash;
}

static int imap_sieve_mailbox_rule_cmp
(const struct imap_sieve_mailbox_rule *rule1,
	const struct imap_sieve_mailbox_rule *rule2)
{
	int ret;

	if ((ret=strcmp(rule1->mailbox, rule2->mailbox)) != 0)
		return ret;
	return null_strcmp(rule1->from, rule2->from);
}

static bool rule_pattern_has_wildcards(const char *pattern)
{
	for (; *pattern != '\0'; pattern++) {
		if (*pattern == '%' || *pattern == '*')
			return TRUE;
	}
	return FALSE;
}

static void
imap_sieve_mailbox_rules_init(struct mail_user *user)
{
	struct imap_sieve_user *isuser = IMAP_SIEVE_USER_CONTEXT(user);
	string_t *identifier;
	unsigned int i = 0;
	size_t prefix_len;

	if (hash_table_is_created(isuser->mbox_rules))
		return;

	hash_table_create(&isuser->mbox_rules, default_pool, 0,
		imap_sieve_mailbox_rule_hash, imap_sieve_mailbox_rule_cmp);
	i_array_init(&isuser->mbox_patterns, 8);

	identifier = t_str_new(256);
	str_append(identifier, "imapsieve_mailbox");
	prefix_len = str_len(identifier);

	for (i = 1; ; i++) {
		struct imap_sieve_mailbox_rule *mbrule;
		const char *setval;
		size_t id_len;

		str_truncate(identifier, prefix_len);
		str_printfa(identifier, "%u", i);
		id_len = str_len(identifier);

		str_append(identifier, "_name");
		setval = mail_user_plugin_getenv
			(user, str_c(identifier));
		if (setval == NULL || *setval == '\0')
			break;

		mbrule = p_new(user->pool,
			struct imap_sieve_mailbox_rule, 1);
		mbrule->index = i;
		mbrule->mailbox = ph_p_str_trim(user->pool, setval, "\t ");

		str_truncate(identifier, id_len);
		str_append(identifier, "_from");
		setval = mail_user_plugin_getenv(user, str_c(identifier));
		if (setval != NULL && *setval != '\0') {
			mbrule->from = ph_p_str_trim(user->pool, setval, "\t ");
			if (strcmp(mbrule->from, "*") == 0)
				mbrule->from = NULL;
		}

		if ((strcmp(mbrule->mailbox, "*") == 0 ||
				!rule_pattern_has_wildcards(mbrule->mailbox)) &&
			(mbrule->from == NULL ||
				!rule_pattern_has_wildcards(mbrule->from)) &&
			hash_table_lookup(isuser->mbox_rules, mbrule) != NULL) {
			imap_sieve_warning(user,
				"Duplicate static mailbox rule [%u] for mailbox `%s' "
				"(skipped)", i, mbrule->mailbox);
			continue;
		}

		str_truncate(identifier, id_len);
		str_append(identifier, "_causes");
		setval = mail_user_plugin_getenv(user, str_c(identifier));
		if (setval != NULL && *setval != '\0') {
			const char *const *cause;

			mbrule->causes = (const char *const *)
				p_strsplit_spaces(user->pool, setval, " \t,");

			for (cause = mbrule->causes; *cause != NULL; cause++) {
				if (!imap_sieve_event_cause_valid(*cause))
					break;
			}
			if (*cause != NULL) {
				imap_sieve_warning(user,
					"Static mailbox rule [%u] has invalid event cause `%s' "
					"(skipped)", i, *cause);
				continue;
			}
		}

		str_truncate(identifier, id_len);
		str_append(identifier, "_before");
		setval = mail_user_plugin_getenv(user, str_c(identifier));
		mbrule->before = p_strdup_empty(user->pool, setval);

		str_truncate(identifier, id_len);
		str_append(identifier, "_after");
		setval = mail_user_plugin_getenv(user, str_c(identifier));
		mbrule->after = p_strdup_empty(user->pool, setval);

		if (user->mail_debug) {
			imap_sieve_debug(user, "Static mailbox rule [%u]: "
				"mailbox=`%s' from=`%s' causes=(%s) => "
				"before=%s after=%s",
				mbrule->index, mbrule->mailbox,
				(mbrule->from == NULL ? "*" : mbrule->from),
				t_strarray_join(mbrule->causes, " "),
				(mbrule->before == NULL ? "(none)" :
					t_strconcat("`", mbrule->before, "'", NULL)),
				(mbrule->after == NULL ? "(none)" :
					t_strconcat("`", mbrule->after, "'", NULL)));
		}

		if ((strcmp(mbrule->mailbox, "*") == 0 ||
				!rule_pattern_has_wildcards(mbrule->mailbox)) &&
			(mbrule->from == NULL ||
				!rule_pattern_has_wildcards(mbrule->from))) {
			hash_table_insert(isuser->mbox_rules, mbrule, mbrule);
		} else {
			array_append(&isuser->mbox_patterns, &mbrule, 1);
		}
	}

	if (i == 0)
		imap_sieve_debug(user, "No static mailbox rules");
}

static bool
imap_sieve_mailbox_rule_match_cause
(struct imap_sieve_mailbox_rule *rule, const char *cause)
{
	const char *const *cp;

	if (rule->causes == NULL || *rule->causes == '\0')
		return TRUE;

	for (cp = rule->causes; *cp != NULL; cp++) {
		if (strcasecmp(cause, *cp) == 0)
			return TRUE;
	}
	return FALSE;
}

static void
imap_sieve_mailbox_rules_match_patterns(struct mail_user *user,
	struct mailbox *dst_box, struct mailbox *src_box,
	const char *cause,
	ARRAY_TYPE(imap_sieve_mailbox_rule) *rules)
{
	struct imap_sieve_user *isuser = IMAP_SIEVE_USER_CONTEXT(user);
	struct imap_sieve_mailbox_rule *const *rule_idx;
	struct mail_namespace *dst_ns, *src_ns;

	if (array_count(&isuser->mbox_patterns) == 0)
		return;

	dst_ns = mailbox_get_namespace(dst_box);
	src_ns = (src_box == NULL ? NULL :
		mailbox_get_namespace(src_box));

	array_foreach(&isuser->mbox_patterns, rule_idx) {
		struct imap_sieve_mailbox_rule *rule = *rule_idx;
		struct imap_match_glob *glob;

		if (src_ns == NULL && rule->from != NULL)
			continue;
		if (!imap_sieve_mailbox_rule_match_cause(rule, cause))
			continue;

		if (strcmp(rule->mailbox, "*") != 0) {
			glob = imap_match_init(pool_datastack_create(),
				rule->mailbox, TRUE, mail_namespace_get_sep(dst_ns));
			if (imap_match(glob, mailbox_get_vname(dst_box))
				!= IMAP_MATCH_YES)
				continue;
		}
		if (rule->from != NULL) {
			glob = imap_match_init(pool_datastack_create(),
				rule->from, TRUE, mail_namespace_get_sep(src_ns));
			if (imap_match(glob, mailbox_get_vname(src_box))
				!= IMAP_MATCH_YES)
				continue;
		}

		imap_sieve_debug(user,
			"Matched static mailbox rule [%u]",
			rule->index);
		array_append(rules, &rule, 1);
	}
}

static void
imap_sieve_mailbox_rules_match(struct mail_user *user,
	const char *dst_box, const char *src_box,
	const char *cause,
	ARRAY_TYPE(imap_sieve_mailbox_rule) *rules)
{
	struct imap_sieve_user *isuser = IMAP_SIEVE_USER_CONTEXT(user);
	struct imap_sieve_mailbox_rule lookup_rule;
	struct imap_sieve_mailbox_rule *rule;

	memset(&lookup_rule, 0, sizeof(lookup_rule));
	lookup_rule.mailbox = dst_box;
	lookup_rule.from = src_box;
	rule = hash_table_lookup(isuser->mbox_rules, &lookup_rule);

	if (rule != NULL &&
		imap_sieve_mailbox_rule_match_cause(rule, cause)) {
		struct imap_sieve_mailbox_rule *const *rule_idx;
		unsigned int insert_idx = 0;

		/* Insert sorted by rule index */
		array_foreach(rules, rule_idx) {
			if (rule->index < (*rule_idx)->index) {
				insert_idx = array_foreach_idx(rules, rule_idx);
				break;
			}
		}
		array_insert(rules, insert_idx, &rule, 1);

		imap_sieve_debug(user,
			"Matched static mailbox rule [%u]",
			rule->index);
	}
}

static void
imap_sieve_mailbox_rules_get(struct mail_user *user,
	struct mailbox *dst_box, struct mailbox *src_box,
	const char *cause,
	ARRAY_TYPE(imap_sieve_mailbox_rule) *rules)
{
	const char *dst_name, *src_name;

	imap_sieve_mailbox_rules_init(user);

	imap_sieve_mailbox_rules_match_patterns
		(user, dst_box, src_box, cause, rules);

	dst_name = mailbox_get_vname(dst_box);
	src_name = (src_box == NULL ? NULL :
		mailbox_get_vname(src_box));

	imap_sieve_mailbox_rules_match
		(user, dst_name, src_name, cause, rules);
	imap_sieve_mailbox_rules_match
		(user, "*", src_name, cause, rules);
	if (src_name != NULL) {
		imap_sieve_mailbox_rules_match
			(user, dst_name, NULL, cause, rules);
		imap_sieve_mailbox_rules_match
			(user, "*", NULL, cause, rules);
	}
}

/*
 * User
 */

static void imap_sieve_user_deinit(struct mail_user *user)
{
	struct imap_sieve_user *isuser = IMAP_SIEVE_USER_CONTEXT(user);

	if (isuser->isieve != NULL)
		imap_sieve_deinit(&isuser->isieve);

	if (hash_table_is_created(isuser->mbox_rules))
		hash_table_destroy(&isuser->mbox_rules);
	if (array_is_created(&isuser->mbox_patterns))
		array_free(&isuser->mbox_patterns);

	isuser->module_ctx.super.deinit(user);
}

static void imap_sieve_user_created(struct mail_user *user)
{
	struct imap_sieve_user *isuser;
	struct mail_user_vfuncs *v = user->vlast;

	isuser = p_new(user->pool, struct imap_sieve_user, 1);
	isuser->module_ctx.super = *v;
	user->vlast = &isuser->module_ctx.super;
	v->deinit = imap_sieve_user_deinit;
	MODULE_CONTEXT_SET(user, imap_sieve_user_module, isuser);
}

/*
 * Hooks
 */

static struct mail_storage_hooks imap_sieve_mail_storage_hooks = {
	.mail_user_created = imap_sieve_user_created,
	.mailbox_allocated = imap_sieve_mailbox_allocated,
	.mail_allocated = imap_sieve_mail_allocated
};

/*
 * Commands
 */

static void imap_sieve_command_pre(struct client_command_context *cmd)
{
	struct client *client = cmd->client;
	struct mail_user *user = client->user;
	struct imap_sieve_user *isuser = 	IMAP_SIEVE_USER_CONTEXT(user);

	if (isuser == NULL)
		return;

	if (strcasecmp(cmd->name, "APPEND") == 0) {
		isuser->cur_cmd = IMAP_SIEVE_CMD_APPEND;
	} else 	if (strcasecmp(cmd->name, "COPY") == 0 ||
		strcasecmp(cmd->name, "UID COPY") == 0) {
		isuser->cur_cmd = IMAP_SIEVE_CMD_COPY;
	} else 	if (strcasecmp(cmd->name, "MOVE") == 0 ||
		strcasecmp(cmd->name, "UID MOVE") == 0) {
		isuser->cur_cmd = IMAP_SIEVE_CMD_MOVE;
	} else 	if (strcasecmp(cmd->name, "STORE") == 0 ||
		strcasecmp(cmd->name, "UID STORE") == 0) {
		isuser->cur_cmd = IMAP_SIEVE_CMD_STORE;
	} else {
		isuser->cur_cmd = IMAP_SIEVE_CMD_OTHER;
	}
}

static void imap_sieve_command_post(struct client_command_context *cmd)
{
	struct client *client = cmd->client;
	struct mail_user *user = client->user;
	struct imap_sieve_user *isuser = 	IMAP_SIEVE_USER_CONTEXT(user);

	if (isuser == NULL)
		return;
	isuser->cur_cmd = IMAP_SIEVE_CMD_NONE;
}

/*
 * Client
 */

void imap_sieve_storage_client_created(struct client *client,
	bool user_script)
{
	struct imap_sieve_user *isuser = IMAP_SIEVE_USER_CONTEXT(client->user);

	isuser->client = client;
	isuser->user_script = user_script;
}

/*
 *
 */

void imap_sieve_storage_init(struct module *module)
{
	command_hook_register(imap_sieve_command_pre, imap_sieve_command_post);
	mail_storage_hooks_add(module, &imap_sieve_mail_storage_hooks);
}

void imap_sieve_storage_deinit(void)
{
	mail_storage_hooks_remove(&imap_sieve_mail_storage_hooks);
	command_hook_unregister(imap_sieve_command_pre, imap_sieve_command_post);
}
