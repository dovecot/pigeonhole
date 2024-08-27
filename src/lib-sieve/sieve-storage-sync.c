/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "str-sanitize.h"
#include "home-expand.h"
#include "eacces-error.h"
#include "mkdir-parents.h"
#include "ioloop.h"
#include "mail-storage-private.h"

#include "sieve-common.h"
#include "sieve-error-private.h"

#include "sieve-script-private.h"
#include "sieve-storage-private.h"

/*
 * Synchronization
 */

int sieve_storage_sync_init(struct sieve_storage *storage,
			    struct mail_user *user)
{
	enum sieve_storage_flags sflags = storage->flags;

	if ((sflags & SIEVE_STORAGE_FLAG_SYNCHRONIZING) == 0 &&
	    (sflags & SIEVE_STORAGE_FLAG_READWRITE) == 0)
		return 0;

	if (!storage->allows_synchronization) {
		if ((sflags & SIEVE_STORAGE_FLAG_SYNCHRONIZING) != 0)
			return -1;
		return 0;
	}

	e_debug(storage->event, "sync: Synchronization active");

	storage->sync_inbox_ns = mail_namespace_find_inbox(user->namespaces);
	return 0;
}

void sieve_storage_sync_deinit(struct sieve_storage *storage ATTR_UNUSED)
{
	/* nothing */
}

/*
 * Sync attributes
 */

static int
sieve_storage_sync_transaction_begin(
	struct sieve_storage *storage,
	struct mailbox_transaction_context **trans_r)
{
	enum mailbox_flags mflags = MAILBOX_FLAG_IGNORE_ACLS;
	struct mail_namespace *ns = storage->sync_inbox_ns;
	struct mailbox *inbox;
	enum mail_error error;

	if (ns == NULL)
		return 0;

	inbox = mailbox_alloc(ns->list, "INBOX", mflags);
	if (mailbox_open(inbox) < 0) {
		e_warning(storage->event, "sync: "
			  "Failed to open user INBOX for attribute modifications: %s",
			  mailbox_get_last_internal_error(inbox, &error));
		mailbox_free(&inbox);
		return -1;
	}

	*trans_r = mailbox_transaction_begin(inbox,
					     MAILBOX_TRANSACTION_FLAG_EXTERNAL,
					     __func__);
	return 1;
}

static int
sieve_storage_sync_transaction_finish(
	struct sieve_storage *storage,
	struct mailbox_transaction_context **trans)
{
	struct mailbox *inbox;
	int ret;

	inbox = mailbox_transaction_get_mailbox(*trans);

	if ((ret = mailbox_transaction_commit(trans)) < 0) {
		enum mail_error error;

		e_warning(storage->event, "sync: "
			  "Failed to update INBOX attributes: %s",
			   mail_storage_get_last_error(
				mailbox_get_storage(inbox), &error));
	}

	mailbox_free(&inbox);
	return ret;
}

int sieve_storage_sync_script_save(struct sieve_storage *storage,
				   const char *name)
{
	struct mailbox_transaction_context *trans;
	const char *key;
	int ret;

	if ((ret = sieve_storage_sync_transaction_begin(storage, &trans)) <= 0)
		return ret;

	key = t_strconcat(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES, name, NULL);

	mail_index_attribute_set(trans->itrans, TRUE, key, ioloop_time, 0);

	return sieve_storage_sync_transaction_finish(storage, &trans);
}

int sieve_storage_sync_script_rename(struct sieve_storage *storage,
				     const char *oldname, const char *newname)
{
	struct mailbox_transaction_context *trans;
	const char *oldkey, *newkey;
	int ret;

	if ((ret = sieve_storage_sync_transaction_begin(storage, &trans)) <= 0)
		return ret;

	oldkey = t_strconcat(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES,
			     oldname, NULL);
	newkey = t_strconcat(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES,
			     newname, NULL);

	mail_index_attribute_unset(trans->itrans, TRUE, oldkey, ioloop_time);
	mail_index_attribute_set(trans->itrans, TRUE, newkey, ioloop_time, 0);

	return sieve_storage_sync_transaction_finish(storage, &trans);
}

int sieve_storage_sync_script_delete(struct sieve_storage *storage,
				     const char *name)
{
	struct mailbox_transaction_context *trans;
	const char *key;
	int ret;

	if ((ret = sieve_storage_sync_transaction_begin(storage, &trans)) <= 0)
		return ret;

	key = t_strconcat(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES, name, NULL);

	mail_index_attribute_unset(trans->itrans, TRUE, key, ioloop_time);

	return sieve_storage_sync_transaction_finish(storage, &trans);
}

int sieve_storage_sync_script_activate(struct sieve_storage *storage)
{
	struct mailbox_transaction_context *trans;
	int ret;

	if ((ret = sieve_storage_sync_transaction_begin(storage, &trans)) <= 0)
		return ret;

	mail_index_attribute_set(trans->itrans, TRUE,
				 MAILBOX_ATTRIBUTE_SIEVE_DEFAULT,
				 ioloop_time, 0);

	return sieve_storage_sync_transaction_finish(storage, &trans);
}

int sieve_storage_sync_deactivate(struct sieve_storage *storage)
{
	struct mailbox_transaction_context *trans;
	int ret;

	if ((ret = sieve_storage_sync_transaction_begin(storage, &trans)) <= 0)
		return ret;

	mail_index_attribute_unset(trans->itrans, TRUE,
				   MAILBOX_ATTRIBUTE_SIEVE_DEFAULT,
				   ioloop_time);

	return sieve_storage_sync_transaction_finish(storage, &trans);
}
