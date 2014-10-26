/* Copyright (c) 2002-2014 Pigeonhole authors, see the included COPYING file
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
#include "sieve-settings.h"
#include "sieve-error-private.h"

#include "sieve-script-private.h"
#include "sieve-storage-private.h"

/*
 * Synchronization
 */

int sieve_storage_sync_init
(struct sieve_storage *storage, struct mail_user *user)
{
	struct mail_namespace *ns;
	struct mailbox *box;
	enum sieve_storage_flags sflags = storage->flags;
	enum mailbox_flags mflags = MAILBOX_FLAG_IGNORE_ACLS;
	enum mail_error error;

	if ( (sflags & SIEVE_STORAGE_FLAG_SYNCHRONIZING) == 0 &&
		(sflags & SIEVE_STORAGE_FLAG_READWRITE) == 0 )
		return 0;

	if ( !storage->allows_synchronization ) {
		if ( (sflags & SIEVE_STORAGE_FLAG_SYNCHRONIZING) != 0 )
			return -1;
		return 0;
	}

	sieve_storage_sys_debug(storage, "sync: "
		"Opening INBOX for attribute modifications");

	ns = mail_namespace_find_inbox(user->namespaces);
	storage->sync_inbox = box = mailbox_alloc(ns->list, "INBOX", mflags);
	if (mailbox_open(box) == 0)
		return 0;

	sieve_storage_sys_warning(storage, "sync: "
		"Failed to open user INBOX for attribute modifications: %s",
		mailbox_get_last_error(box, &error));
	return -1;
}

void sieve_storage_sync_deinit
(struct sieve_storage *storage)
{
	if (storage->sync_inbox != NULL)
		mailbox_free(&storage->sync_inbox);
}

/*
 * Sync attributes
 */

static void sieve_storage_sync_transaction_finish
(struct sieve_storage *storage, struct mailbox_transaction_context **t)
{
	struct mailbox *inbox = storage->sync_inbox;

	i_assert( storage->sync_inbox != NULL );

	if (mailbox_transaction_commit(t) < 0) {
		enum mail_error error;
		
		sieve_storage_sys_warning(storage, "sync: "
			"Failed to update INBOX attributes: %s",
			mail_storage_get_last_error(mailbox_get_storage(inbox), &error));
	}
}

void sieve_storage_sync_script_save
(struct sieve_storage *storage, const char *name)
{
	struct mailbox_transaction_context *t;
	const char *key;

	if (storage->sync_inbox == NULL)
		return;

	key = t_strconcat
		(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES, name, NULL);
	t = mailbox_transaction_begin(storage->sync_inbox, 0);	
	mail_index_attribute_set(t->itrans, TRUE, key, ioloop_time, 0);
	sieve_storage_sync_transaction_finish(storage, &t);
}

void sieve_storage_sync_script_rename
(struct sieve_storage *storage, const char *oldname,
	const char *newname)
{
	struct mailbox_transaction_context *t;
	const char *oldkey, *newkey;

	if (storage->sync_inbox == NULL)
		return;

	oldkey = t_strconcat
		(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES, oldname, NULL);
	newkey = t_strconcat
		(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES, newname, NULL);

	t = mailbox_transaction_begin(storage->sync_inbox, 0);	
	mail_index_attribute_unset(t->itrans, TRUE, oldkey, ioloop_time);
	mail_index_attribute_set(t->itrans, TRUE, newkey, ioloop_time, 0);
	sieve_storage_sync_transaction_finish(storage, &t);
}

void sieve_storage_sync_script_delete
(struct sieve_storage *storage, const char *name)
{
	struct mailbox_transaction_context *t;
	const char *key;

	if (storage->sync_inbox == NULL)
		return;

	key = t_strconcat
		(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES, name, NULL);
	
	t = mailbox_transaction_begin(storage->sync_inbox, 0);	
	mail_index_attribute_unset(t->itrans, TRUE, key, ioloop_time);
	sieve_storage_sync_transaction_finish(storage, &t);
}
