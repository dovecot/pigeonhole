/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "mempool.h"
#include "imem.h"
#include "array.h"
#include "strfuncs.h"
#include "str-sanitize.h"
#include "abspath.h"
#include "unlink-directory.h"
#include "env-util.h"
#include "mail-namespace.h"
#include "mail-storage.h"
#include "imap-metadata.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-interpreter.h"

#include "testsuite-message.h"
#include "testsuite-common.h"
#include "testsuite-smtp.h"

#include "testsuite-mailstore.h"

#include <sys/stat.h>
#include <sys/types.h>

/*
 * Forward declarations
 */

static void testsuite_mailstore_close(void);

/*
 * State
 */

static struct mail_user *testsuite_mailstore_user = NULL;

static char *testsuite_mailstore_location = NULL;
static char *testsuite_mailstore_attrs = NULL;

static char *testsuite_mailstore_folder = NULL;
static struct mailbox *testsuite_mailstore_box = NULL;
static struct mailbox_transaction_context *testsuite_mailstore_trans = NULL;
static struct mail *testsuite_mailstore_mail = NULL;

/*
 * Initialization
 */

void testsuite_mailstore_init(void)
{
	struct mail_user *mail_user_dovecot, *mail_user;
	struct mail_namespace *ns;
	struct mail_namespace_settings *ns_set;
	struct mail_storage_settings *mail_set;
	const char *tmpdir, *error;

	tmpdir = testsuite_tmp_dir_get();
	testsuite_mailstore_location =
		i_strconcat(tmpdir, "/mailstore", NULL);
	testsuite_mailstore_attrs =
		i_strconcat(tmpdir, "/mail-attrs.dict", NULL);

	if ( mkdir(testsuite_mailstore_location, 0700) < 0 ) {
		i_fatal("failed to create temporary directory '%s': %m.",
			testsuite_mailstore_location);
	}
	
	mail_user_dovecot = sieve_tool_get_mail_user(sieve_tool);
	mail_user = mail_user_alloc("testsuite mail user",
		mail_user_dovecot->set_info, mail_user_dovecot->unexpanded_set);
	mail_user->autocreated = TRUE;
	mail_user_set_home(mail_user, t_abspath(""));
	if (mail_user_init(mail_user, &error) < 0)
		i_fatal("Testsuite user initialization failed: %s", error);

	ns_set = p_new(mail_user->pool, struct mail_namespace_settings, 1);
	ns_set->location = testsuite_mailstore_location;
	ns_set->separator = ".";

	ns = mail_namespaces_init_empty(mail_user);
	ns->flags |= NAMESPACE_FLAG_INBOX_USER;
	ns->set = ns_set;
	/* absolute paths are ok with raw storage */
	mail_set = p_new(mail_user->pool, struct mail_storage_settings, 1);
	*mail_set = *ns->mail_set;
	mail_set->mail_location = p_strconcat
		(mail_user->pool, "maildir:", testsuite_mailstore_location, NULL);
	mail_set->mail_attribute_dict = p_strconcat
		(mail_user->pool, "file:", testsuite_mailstore_attrs, NULL);
	mail_set->mail_full_filesystem_access = TRUE;
	ns->mail_set = mail_set;

	if (mail_storage_create(ns, "maildir", 0, &error) < 0)
		i_fatal("Couldn't create testsuite storage: %s", error);
	if (mail_namespaces_init_finish(ns, &error) < 0)
		i_fatal("Couldn't create testsuite namespace: %s", error);

	testsuite_mailstore_user = mail_user;
}

void testsuite_mailstore_deinit(void)
{
	testsuite_mailstore_close();

	if ( unlink_directory(testsuite_mailstore_location, TRUE) < 0 ) {
		i_warning("failed to remove temporary directory '%s': %m.",
			testsuite_mailstore_location);
	}

	i_free(testsuite_mailstore_location);
	i_free(testsuite_mailstore_attrs);
	mail_user_unref(&testsuite_mailstore_user);
}

void testsuite_mailstore_reset(void)
{
}

/*
 * Mail user
 */

struct mail_user *testsuite_mailstore_get_user(void)
{
	if (testsuite_mailstore_user == NULL)
		return sieve_tool_get_mail_user(sieve_tool);
	return testsuite_mailstore_user;
}

/*
 * Mailbox Access
 */

bool testsuite_mailstore_mailbox_create
(const struct sieve_runtime_env *renv ATTR_UNUSED, const char *folder)
{
	struct mail_user *mail_user = testsuite_mailstore_user;
	struct mail_namespace *ns = mail_user->namespaces;
	struct mailbox *box;

	box = mailbox_alloc(ns->list, folder, 0);

	if ( mailbox_create(box, NULL, FALSE) < 0 ) {
		mailbox_free(&box);
		return FALSE;
	}

	mailbox_free(&box);

	return TRUE;
}

static void testsuite_mailstore_close(void)
{
	if ( testsuite_mailstore_mail != NULL )
		mail_free(&testsuite_mailstore_mail);

	if ( testsuite_mailstore_trans != NULL )
		mailbox_transaction_rollback(&testsuite_mailstore_trans);

	if ( testsuite_mailstore_box != NULL )
		mailbox_free(&testsuite_mailstore_box);

	if ( testsuite_mailstore_folder != NULL )
		i_free(testsuite_mailstore_folder);
}

static struct mail *testsuite_mailstore_open(const char *folder)
{
	enum mailbox_flags flags =
		MAILBOX_FLAG_SAVEONLY | MAILBOX_FLAG_POST_SESSION;
	struct mail_user *mail_user = testsuite_mailstore_user;
	struct mail_namespace *ns = mail_user->namespaces;
	struct mailbox *box;
	struct mailbox_transaction_context *t;

	if ( testsuite_mailstore_mail == NULL ) {
		testsuite_mailstore_close();
	} else if ( testsuite_mailstore_folder != NULL
		&& strcmp(folder, testsuite_mailstore_folder) != 0  ) {
		testsuite_mailstore_close();
	} else {
		return testsuite_mailstore_mail;
	}

	box = mailbox_alloc(ns->list, folder, flags);
	if ( mailbox_open(box) < 0 ) {
		sieve_sys_error(testsuite_sieve_instance,
			"testsuite: failed to open mailbox '%s'", folder);
		mailbox_free(&box);
		return NULL;
	}

	/* Sync mailbox */

	if ( mailbox_sync(box, MAILBOX_SYNC_FLAG_FULL_READ) < 0 ) {
		sieve_sys_error(testsuite_sieve_instance,
			"testsuite: failed to sync mailbox '%s'", folder);
		mailbox_free(&box);
		return NULL;
	}

	/* Start transaction */

	t = mailbox_transaction_begin(box, 0);

	testsuite_mailstore_folder = i_strdup(folder);
	testsuite_mailstore_box = box;
	testsuite_mailstore_trans = t;
	testsuite_mailstore_mail = mail_alloc(t, 0, NULL);

	return testsuite_mailstore_mail;
}

bool testsuite_mailstore_mail_index
(const struct sieve_runtime_env *renv, const char *folder, unsigned int index)
{
	struct mail *mail = testsuite_mailstore_open(folder);

	if ( mail == NULL )
		return FALSE;

	mail_set_seq(mail, index+1);
	testsuite_message_set_mail(renv, mail);

	return TRUE;
}

/*
 * IMAP metadata
 */

int testsuite_mailstore_set_imap_metadata
(const char *mailbox, const char *annotation, const char *value)
{
	struct imap_metadata_transaction *imtrans;
	struct mail_attribute_value avalue;
	struct mailbox *box;
	enum mail_error error_code;
	const char *error;
	int ret;

	if ( !imap_metadata_verify_entry_name(annotation, &error) ) {
		sieve_sys_error(testsuite_sieve_instance,
			"testsuite: imap metadata: "
			"specified annotation name `%s' is invalid: %s",
			str_sanitize(annotation, 256), error);
		return -1;
	}

	if ( mailbox != NULL ) {
		struct mail_namespace *ns;
		ns = mail_namespace_find
			(testsuite_mailstore_user->namespaces, mailbox);
		box = mailbox_alloc(ns->list, mailbox, 0);
		imtrans = imap_metadata_transaction_begin(box);
	} else {
		box = NULL;
		imtrans = imap_metadata_transaction_begin_server
			(testsuite_mailstore_user);
	}

	i_zero(&avalue);
	avalue.value = value;
	if ((ret=imap_metadata_set(imtrans, annotation, &avalue)) < 0) {
		error = imap_metadata_transaction_get_last_error
			(imtrans, &error_code);
		imap_metadata_transaction_rollback(&imtrans);
	} else {
		ret = imap_metadata_transaction_commit
			(&imtrans, &error_code, &error);
	}
	if ( box != NULL )
		mailbox_free(&box);

	if ( ret < 0 ) {
		sieve_sys_error(testsuite_sieve_instance,
			"testsuite: imap metadata: "
			"failed to assign annotation `%s': %s",
			str_sanitize(annotation, 256), error);
		return -1;
	}
	return 0;
}
