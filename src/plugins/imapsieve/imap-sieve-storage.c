/* Copyright (c) 2016-2018 Pigeonhole authors, see the included COPYING file */

#include "imap-common.h"
#include "array.h"
#include "hash.h"
#include "str.h"
#include "istream.h"
#include "ostream.h"
#include "module-context.h"
#include "settings.h"
#include "mail-user.h"
#include "mail-storage-private.h"
#include "mailbox-attribute.h"
#include "mailbox-list-private.h"
#include "imap-match.h"
#include "imap-util.h"

#include "imap-sieve.h"
#include "imap-sieve-settings.h"
#include "imap-sieve-storage.h"

#define MAILBOX_ATTRIBUTE_IMAPSIEVE_SCRIPT "imapsieve/script"
#define MAIL_SERVER_ATTRIBUTE_IMAPSIEVE_SCRIPT "imapsieve/script"

#define IMAP_SIEVE_USER_CONTEXT(obj) \
	MODULE_CONTEXT(obj, imap_sieve_user_module)
#define IMAP_SIEVE_USER_CONTEXT_REQUIRE(obj) \
	MODULE_CONTEXT_REQUIRE(obj, imap_sieve_user_module)
#define IMAP_SIEVE_CONTEXT(obj) \
	MODULE_CONTEXT(obj, imap_sieve_storage_module)
#define IMAP_SIEVE_CONTEXT_REQUIRE(obj) \
	MODULE_CONTEXT_REQUIRE(obj, imap_sieve_storage_module)
#define IMAP_SIEVE_MAIL_CONTEXT(obj) \
	MODULE_CONTEXT_REQUIRE(obj, imap_sieve_mail_module)

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

ARRAY_DEFINE_TYPE(imap_sieve_mailbox_event,
	struct imap_sieve_mailbox_event);

struct imap_sieve_user {
	union mail_user_module_context module_ctx;
	struct client *client;
	struct imap_sieve *isieve;
	struct event *event;

	enum imap_sieve_command cur_cmd;

	bool sieve_active:1;
	bool user_script:1;
	bool expunge_discarded:1;
};

struct imap_sieve_mailbox {
	union mailbox_module_context module_ctx;
	struct imap_sieve_user *user;

	struct event *event;
};

struct imap_sieve_mailbox_event {
	uint32_t dest_mail_uid, src_mail_uid;
	unsigned int save_seq;

	const char *changed_flags;
};

struct imap_sieve_mailbox_transaction {
	pool_t pool;

	union mailbox_transaction_module_context module_ctx;

	struct mailbox *src_box;
	struct mailbox_transaction_context *src_mail_trans;

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

struct event_category event_category_imap_sieve = {
	.name = "imapsieve",
};

/*
 * Events
 */

static int
imap_sieve_mailbox_get_script_real(struct mailbox *box,
				   const char **script_name_r)
{
	struct mail_user *user = box->storage->user;
	struct imap_sieve_mailbox *isbox = IMAP_SIEVE_CONTEXT_REQUIRE(box);
	struct mail_attribute_value value;
	int ret;

	*script_name_r = NULL;

	/* Get the name of the Sieve script from mailbox METADATA. */
	ret = mailbox_attribute_get(box, MAIL_ATTRIBUTE_TYPE_SHARED,
				    MAILBOX_ATTRIBUTE_IMAPSIEVE_SCRIPT, &value);
	if (ret < 0) {
		e_error(isbox->event, "Failed to read /shared/"
			MAILBOX_ATTRIBUTE_IMAPSIEVE_SCRIPT" "
			"mailbox attribute: %s",
			mailbox_get_last_internal_error(box, NULL));
		return -1;
	}

	if (ret > 0) {
		e_debug(isbox->event, "Mailbox attribute /shared/"
			MAILBOX_ATTRIBUTE_IMAPSIEVE_SCRIPT" "
			"points to Sieve script '%s'", value.value);
	/* If not found, get the name of the Sieve script from server METADATA.
	 */
	} else {
		struct mail_namespace *ns;
		struct mailbox *inbox;

		e_debug(isbox->event, "Mailbox attribute /shared/"
			MAILBOX_ATTRIBUTE_IMAPSIEVE_SCRIPT" "
			"not found");

		ns = mail_namespace_find_inbox(user->namespaces);
		inbox = mailbox_alloc(ns->list, "INBOX", MAILBOX_FLAG_READONLY);
		ret = mailbox_attribute_get(
			inbox, MAIL_ATTRIBUTE_TYPE_SHARED,
			MAILBOX_ATTRIBUTE_PREFIX_DOVECOT_PVT_SERVER
			MAILBOX_ATTRIBUTE_IMAPSIEVE_SCRIPT, &value);

		if (ret <= 0) {
			if (ret < 0) {
				e_error(isbox->event, "Failed to read /shared/"
					MAIL_SERVER_ATTRIBUTE_IMAPSIEVE_SCRIPT" "
					"server attribute: %s",
					mailbox_get_last_internal_error(inbox, NULL));
			} else if (ret == 0) {
				e_debug(isbox->event,
					"Server attribute /shared/"
					MAIL_SERVER_ATTRIBUTE_IMAPSIEVE_SCRIPT" "
					"not found");
			}
			mailbox_free(&inbox);
			return ret;
		}
		mailbox_free(&inbox);

		e_debug(isbox->event, "Server attribute /shared/"
			MAIL_SERVER_ATTRIBUTE_IMAPSIEVE_SCRIPT" "
			"points to Sieve script '%s'", value.value);
	}

	*script_name_r = value.value;
	return 1;
}

static int
imap_sieve_mailbox_get_script(struct mailbox *box, const char **script_name_r)
{
	int ret;

	ret = imap_sieve_mailbox_get_script_real(box, script_name_r);
	return ret;
}

static struct imap_sieve_mailbox_event *
imap_sieve_create_mailbox_event(struct mailbox_transaction_context *t,
				struct mail *dest_mail)
{
	struct imap_sieve_mailbox_transaction *ismt =
		IMAP_SIEVE_CONTEXT_REQUIRE(t);
	struct imap_sieve_mailbox_event *event;

	if (!array_is_created(&ismt->events))
		i_array_init(&ismt->events, 64);

	event = array_append_space(&ismt->events);
	event->save_seq = t->save_count;
	event->dest_mail_uid = dest_mail->uid;
	return event;
}

static void
imap_sieve_add_mailbox_event(struct mailbox_transaction_context *t,
			     struct mail *dest_mail, struct mailbox *src_box,
			     const char *changed_flags)
{
	struct imap_sieve_mailbox_transaction *ismt =
		IMAP_SIEVE_CONTEXT_REQUIRE(t);
	struct imap_sieve_mailbox_event *event;

	i_assert(ismt->src_box == NULL || ismt->src_box == src_box);
	ismt->src_box = src_box;

	event = imap_sieve_create_mailbox_event(t, dest_mail);
	event->changed_flags = p_strdup(ismt->pool, changed_flags);
}

static void
imap_sieve_add_mailbox_copy_event(struct mailbox_transaction_context *t,
				  struct mail *dest_mail, struct mail *src_mail)
{
	struct imap_sieve_mailbox_transaction *ismt =
		IMAP_SIEVE_CONTEXT_REQUIRE(t);
	struct imap_sieve_mailbox_event *event;

	i_assert(ismt->src_box == NULL || ismt->src_box == src_mail->box);
	i_assert(ismt->src_mail_trans == NULL ||
		 ismt->src_mail_trans == src_mail->transaction);

	ismt->src_box = src_mail->box;
	ismt->src_mail_trans = src_mail->transaction;

	event = imap_sieve_create_mailbox_event(t, dest_mail);
	event->src_mail_uid = src_mail->uid;
}

/*
 * Mail
 */

static void
imap_sieve_mail_update_flags(struct mail *_mail, enum modify_type modify_type,
			     enum mail_flags flags)
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
				enum modify_type modify_type,
				struct mail_keywords *keywords)
{
	struct mail_private *mail = (struct mail_private *)_mail;
	struct imap_sieve_mail *ismail = IMAP_SIEVE_MAIL_CONTEXT(mail);
	const char *const *old_keywords, *const *new_keywords;
	unsigned int i, j;

	old_keywords = mail_get_keywords(_mail);
	ismail->module_ctx.super.update_keywords(_mail, modify_type, keywords);
	new_keywords = mail_get_keywords(_mail);

	if (ismail->flags == NULL)
		ismail->flags = str_new(default_pool, 64);

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
	struct imap_sieve_mailbox *isbox =
		IMAP_SIEVE_CONTEXT_REQUIRE(_mail->box);
	struct imap_sieve_mail *ismail = IMAP_SIEVE_MAIL_CONTEXT(mail);

	if (ismail->flags != NULL && str_len(ismail->flags) > 0) {
		if (!_mail->expunged) {
			e_debug(isbox->event, "FLAG event (changed flags: %s)",
				str_c(ismail->flags));

			imap_sieve_add_mailbox_event(
				t, _mail, _mail->box, str_c(ismail->flags));
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

	str_free(&flags);
}

static void imap_sieve_mail_allocated(struct mail *_mail)
{
	struct mail_private *mail = (struct mail_private *)_mail;
	struct imap_sieve_mailbox_transaction *ismt =
		IMAP_SIEVE_CONTEXT(_mail->transaction);
	struct mail_user *user = _mail->box->storage->user;
	struct imap_sieve_user *isuser =
		IMAP_SIEVE_USER_CONTEXT_REQUIRE(user);
	struct mail_vfuncs *v = mail->vlast;
	struct imap_sieve_mail *ismail;

	if (ismt == NULL || isuser->sieve_active)
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
	struct imap_sieve_user *isuser = IMAP_SIEVE_USER_CONTEXT_REQUIRE(user);
	struct imap_sieve_mailbox *isbox = IMAP_SIEVE_CONTEXT_REQUIRE(t->box);
	struct imap_sieve_mailbox_transaction *ismt = IMAP_SIEVE_CONTEXT(t);

	if (isbox->module_ctx.super.copy(ctx, mail) < 0)
		return -1;

	if (ismt != NULL && !isuser->sieve_active &&
	    !ctx->dest_mail->expunged &&
	    (isuser->cur_cmd == IMAP_SIEVE_CMD_COPY ||
	     isuser->cur_cmd == IMAP_SIEVE_CMD_MOVE)) {
		e_debug(isbox->event, "%s event",
			(isuser->cur_cmd == IMAP_SIEVE_CMD_COPY ?
			 "COPY" : "MOVE"));
		imap_sieve_add_mailbox_copy_event(t, ctx->dest_mail,
						  ctx->copy_src_mail);
	}

	return 0;
}

static int imap_sieve_mailbox_save_finish(struct mail_save_context *ctx)
{
	struct mailbox_transaction_context *t = ctx->transaction;
	struct mailbox *box = t->box;
	struct imap_sieve_mailbox_transaction *ismt = IMAP_SIEVE_CONTEXT(t);
	struct imap_sieve_mailbox *isbox = IMAP_SIEVE_CONTEXT_REQUIRE(box);
	struct mail_user *user = box->storage->user;
	struct imap_sieve_user *isuser = IMAP_SIEVE_USER_CONTEXT_REQUIRE(user);
	struct mail *dest_mail = ctx->copying_via_save ? NULL : ctx->dest_mail;

	if (isbox->module_ctx.super.save_finish(ctx) < 0)
		return -1;

	if (ismt != NULL && !isuser->sieve_active &&
	    dest_mail != NULL && !dest_mail->expunged &&
	    isuser->cur_cmd == IMAP_SIEVE_CMD_APPEND) {
		e_debug(isbox->event, "APPEND event");
		imap_sieve_add_mailbox_event(t, dest_mail, box, NULL);
	}
	return 0;
}

/*
 * Mailbox
 */

static struct mailbox_transaction_context *
imap_sieve_mailbox_transaction_begin(struct mailbox *box,
				     enum mailbox_transaction_flags flags,
				     const char *reason)
{
	struct imap_sieve_mailbox *isbox = IMAP_SIEVE_CONTEXT_REQUIRE(box);
	struct mail_user *user = box->storage->user;
	struct imap_sieve_user *isuser = IMAP_SIEVE_USER_CONTEXT(user);
	struct mailbox_transaction_context *t;
	struct imap_sieve_mailbox_transaction *ismt;
	pool_t pool;

	/* Commence parent transaction */
	t = isbox->module_ctx.super.transaction_begin(box, flags, reason);

	if (isuser == NULL || isuser->sieve_active ||
	    isuser->cur_cmd == IMAP_SIEVE_CMD_NONE)
		return t;

	i_assert(isuser->client != NULL);

	pool = pool_alloconly_create("imap_sieve_mailbox_transaction", 1024);
	ismt = p_new(pool, struct imap_sieve_mailbox_transaction, 1);
	ismt->pool = pool;
	MODULE_CONTEXT_SET(t, imap_sieve_storage_module, ismt);

	return t;
}

static void
imap_sieve_mailbox_transaction_free(struct imap_sieve_mailbox_transaction *ismt)
{
	if (array_is_created(&ismt->events))
		array_free(&ismt->events);
	pool_unref(&ismt->pool);
}

static void
imap_sieve_mailbox_run_copy_source(
	struct imap_sieve_mailbox_transaction *ismt,
	struct imap_sieve_run *isrun,
	const struct imap_sieve_mailbox_event *mevent, struct mail **src_mail,
	bool *fatal_r)
{
	struct mailbox *src_box = ismt->src_box;
	struct imap_sieve_mailbox *src_isbox =
		IMAP_SIEVE_CONTEXT_REQUIRE(src_box);
	int ret;

	*fatal_r = FALSE;

	if (isrun == NULL)
		return;

	i_assert(ismt->src_mail_trans->box == src_box);
	i_assert(mevent->src_mail_uid > 0);

	if (*src_mail == NULL)
		*src_mail = mail_alloc(ismt->src_mail_trans, 0, NULL);

	/* Select source message */
	if (!mail_set_uid(*src_mail, mevent->src_mail_uid)) {
		e_warning(src_isbox->event,
			  "Failed to find source message for Sieve event "
			  "(UID=%llu)",
			  (unsigned long long)mevent->src_mail_uid);
		return;
	}

	e_debug(src_isbox->event, "Running copy_source_after scripts.");

	/* Run scripts for source mail */
	ret = imap_sieve_run_mail(isrun, *src_mail, NULL, fatal_r);
	if (ret > 0) {
		/* Discard */
		mail_update_flags(*src_mail, MODIFY_ADD, MAIL_DELETED);
	}
}

static int
imap_sieve_mailbox_transaction_run(
	struct imap_sieve_mailbox_transaction *ismt, struct mailbox *dest_box,
	struct mail_transaction_commit_changes *changes)
{
	static const char *wanted_headers[] = {
		"From", "To", "Message-ID", "Subject", "Return-Path",
		NULL
	};
	struct mailbox *src_box = ismt->src_box;
	struct mail_user *user = dest_box->storage->user;
	struct imap_sieve_user *isuser = IMAP_SIEVE_USER_CONTEXT_REQUIRE(user);
	struct imap_sieve_mailbox *dest_isbox =
		IMAP_SIEVE_CONTEXT_REQUIRE(dest_box);
	const struct imap_sieve_mailbox_event *mevent;
	struct mailbox_header_lookup_ctx *headers_ctx;
	struct mailbox_transaction_context *st;
	struct mailbox *sbox;
	struct imap_sieve_run *isrun, *isrun_src;
	struct seq_range_iter siter;
	const char *cause, *script_name = NULL;
	bool can_discard;
	struct mail *mail, *src_mail = NULL;
	int ret;

	if (ismt == NULL || !array_is_created(&ismt->events)) {
		/* Nothing to do */
		return 0;
	}

	i_assert(isuser->client != NULL);

	/* Get user script for this mailbox */
	if (isuser->user_script &&
	    imap_sieve_mailbox_get_script(dest_box, &script_name) < 0) {
		// FIXME: some errors may warrant -1
		return 0;
	}

	/* Make sure IMAPSIEVE is initialized for this user */
	if (isuser->isieve == NULL)
		isuser->isieve = imap_sieve_init(isuser->client);

	can_discard = FALSE;
	switch (isuser->cur_cmd) {
	case IMAP_SIEVE_CMD_APPEND:
		cause = "APPEND";
		can_discard = TRUE;
		break;
	case IMAP_SIEVE_CMD_COPY:
	case IMAP_SIEVE_CMD_MOVE:
		i_assert(src_box != NULL);
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
		/* Initialize */
		ret = imap_sieve_run_init(isuser->isieve, dest_isbox->event,
					  dest_box, src_box, cause, script_name,
					  SIEVE_STORAGE_TYPE_BEFORE,
					  SIEVE_STORAGE_TYPE_AFTER, &isrun);

		/* Initialize source script execution */
		isrun_src = NULL;
		if (ret > 0 && ismt->src_mail_trans != NULL &&
		    isuser->cur_cmd == IMAP_SIEVE_CMD_COPY) {
			if (imap_sieve_run_init(
				isuser->isieve, dest_isbox->event,
				dest_box, src_box, cause, NULL,
				NULL, "copy-source-after", &isrun_src) <= 0)
				isrun_src = NULL;
		}
	} T_END;

	if (ret <= 0) {
		// FIXME: temp fail should be handled properly
		return 0;
	}

	/* Get synchronized view on the destination mailbox */
	sbox = mailbox_alloc(dest_box->list, dest_box->vname, 0);
	if (mailbox_sync(sbox, 0) < 0) {
		mailbox_free(&sbox);
		imap_sieve_run_deinit(&isrun);
		if (isrun_src != NULL)
			imap_sieve_run_deinit(&isrun_src);
		return -1;
	}

	/* Create transaction for event messages */
	st = mailbox_transaction_begin(sbox, 0, __func__);
	headers_ctx = mailbox_header_lookup_init(sbox, wanted_headers);
	mail = mail_alloc(st, 0, headers_ctx);
	mailbox_header_lookup_unref(&headers_ctx);

	/* Iterate through all events */
	seq_range_array_iter_init(&siter, &changes->saved_uids);
	array_foreach(&ismt->events, mevent) {
		uint32_t uid;
		bool fatal;

		/* Determine UID for saved message */
		if (mevent->dest_mail_uid > 0)
			uid = mevent->dest_mail_uid;
		else if (!seq_range_array_iter_nth(&siter, mevent->save_seq,
						   &uid)) {
			/* already gone for some reason */
			e_debug(dest_isbox->event,
				"Message for Sieve event gone");
			continue;
		}

		/* Select event message */
		i_assert(uid > 0);
		if (!mail_set_uid(mail, uid) || mail->expunged) {
			/* Already gone for some reason */
			e_debug(dest_isbox->event,
				"Message for Sieve event gone (UID=%llu)",
				(unsigned long long)uid);
			continue;
		}

		/* Run scripts for this mail */
		ret = imap_sieve_run_mail(isrun, mail, mevent->changed_flags,
					  &fatal);
		if (fatal)
			break;

		/* Handle the result */
		if (ret < 0) {
			/* Sieve error; keep */
		} else {
			if (ret <= 0 || !can_discard) {
				/* Keep */
			} else if (!isuser->expunge_discarded) {
				/* Mark as \Deleted */
				mail_update_flags(mail,
						  MODIFY_ADD, MAIL_DELETED);
			} else {
				/* Expunge */
				mail_expunge(mail);
			}

			imap_sieve_mailbox_run_copy_source(
				ismt, isrun_src, mevent, &src_mail, &fatal);
			if (fatal)
				break;
		}
	}

	/* Cleanup */
	mail_free(&mail);
	ret = mailbox_transaction_commit(&st);
	if (src_mail != NULL)
		mail_free(&src_mail);
	imap_sieve_run_deinit(&isrun);
	if (isrun_src != NULL)
		imap_sieve_run_deinit(&isrun_src);
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
	struct imap_sieve_mailbox *isbox = IMAP_SIEVE_CONTEXT_REQUIRE(t->box);
	struct imap_sieve_user *isuser = IMAP_SIEVE_USER_CONTEXT_REQUIRE(user);
	int ret = 0;

	if ((isbox->module_ctx.super.transaction_commit(t, changes_r)) < 0)
		ret = -1;
	else if (ismt != NULL) {
		isuser->sieve_active = TRUE;
		if (imap_sieve_mailbox_transaction_run(
			ismt, box, changes_r) < 0)
			ret = -1;
		isuser->sieve_active = FALSE;
	}

	if (ismt != NULL)
		imap_sieve_mailbox_transaction_free(ismt);
	return ret;
}

static void
imap_sieve_mailbox_transaction_rollback(struct mailbox_transaction_context *t)
{
	struct imap_sieve_mailbox_transaction *ismt = IMAP_SIEVE_CONTEXT(t);
	struct imap_sieve_mailbox *isbox = IMAP_SIEVE_CONTEXT_REQUIRE(t->box);

	isbox->module_ctx.super.transaction_rollback(t);

	if (ismt != NULL)
		imap_sieve_mailbox_transaction_free(ismt);
}

static void imap_sieve_mailbox_free(struct mailbox *box)
{
	struct imap_sieve_mailbox *isbox = IMAP_SIEVE_CONTEXT_REQUIRE(box);

	event_unref(&isbox->event);

	isbox->module_ctx.super.free(box);
}

static void imap_sieve_mailbox_allocated(struct mailbox *box)
{
	struct mail_user *user = box->storage->user;
	struct imap_sieve_user *isuser = IMAP_SIEVE_USER_CONTEXT_REQUIRE(user);
	struct mailbox_vfuncs *v = box->vlast;
	struct imap_sieve_mailbox *isbox;

	if (isuser->client == NULL || isuser->sieve_active)
		return;

	struct event *event;

	event = event_create(box->event);
	event_add_category(event, &event_category_imap_sieve);
	event_set_append_log_prefix(event, "imapsieve: ");

	isbox = p_new(box->pool, struct imap_sieve_mailbox, 1);
	isbox->user = isuser;
	isbox->event = event;
	isbox->module_ctx.super = *v;
	box->vlast = &isbox->module_ctx.super;

	v->copy = imap_sieve_mailbox_copy;
	v->save_finish = imap_sieve_mailbox_save_finish;
	v->transaction_begin = imap_sieve_mailbox_transaction_begin;
	v->transaction_commit = imap_sieve_mailbox_transaction_commit;
	v->transaction_rollback = imap_sieve_mailbox_transaction_rollback;
	v->free = imap_sieve_mailbox_free;
	MODULE_CONTEXT_SET(box, imap_sieve_storage_module, isbox);
}

/*
 * User
 */

static void imap_sieve_user_deinit(struct mail_user *user)
{
	struct imap_sieve_user *isuser = IMAP_SIEVE_USER_CONTEXT_REQUIRE(user);

	if (isuser->isieve != NULL)
		imap_sieve_deinit(&isuser->isieve);

	event_unref(&isuser->event);

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

	isuser->event = event_create(user->event);
	event_add_category(isuser->event, &event_category_imap_sieve);
	event_set_append_log_prefix(isuser->event, "imapsieve: ");
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
	} else if (strcasecmp(cmd->name, "COPY") == 0 ||
		   strcasecmp(cmd->name, "UID COPY") == 0) {
		isuser->cur_cmd = IMAP_SIEVE_CMD_COPY;
	} else if (strcasecmp(cmd->name, "MOVE") == 0 ||
		   strcasecmp(cmd->name, "UID MOVE") == 0) {
		isuser->cur_cmd = IMAP_SIEVE_CMD_MOVE;
	} else if (strcasecmp(cmd->name, "STORE") == 0 ||
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
	struct imap_sieve_user *isuser = IMAP_SIEVE_USER_CONTEXT(user);

	if (isuser == NULL)
		return;
	isuser->cur_cmd = IMAP_SIEVE_CMD_NONE;
}

/*
 * Client
 */

void imap_sieve_storage_client_created(struct client *client)
{
	struct mail_user *user = client->user;
	struct imap_sieve_user *isuser = IMAP_SIEVE_USER_CONTEXT_REQUIRE(user);
	const struct imap_sieve_settings *set;
	const char *error;

	if (settings_get(client->event, &imap_sieve_setting_parser_info, 0,
			 &set, &error) < 0) {
		e_error(client->event, "%s", error);
		return;
	}

	if (*set->url != '\0') {
		client_add_capability(client,
			t_strconcat("IMAPSIEVE=", set->url, NULL));
	}

	isuser->client = client;
	isuser->user_script = (*set->url != '\0');
	isuser->expunge_discarded = set->expunge_discarded;
	settings_free(set);
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
	command_hook_unregister(imap_sieve_command_pre,
				imap_sieve_command_post);
}
