/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "mempool.h"
#include "imem.h"
#include "array.h"
#include "strfuncs.h"
#include "str-sanitize.h"
#include "path-util.h"
#include "unlink-directory.h"
#include "env-util.h"
#include "mail-storage-service.h"
#include "mail-namespace.h"
#include "mail-storage.h"
#include "imap-metadata.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-actions.h"
#include "sieve-interpreter.h"

#include "testsuite-message.h"
#include "testsuite-common.h"
#include "testsuite-smtp.h"

#include "testsuite-mailstore.h"

#include <sys/stat.h>
#include <sys/types.h>

struct testsuite_mailstore_mail {
	struct testsuite_mailstore_mail *next;

	char *folder;
	struct mailbox *box;
	struct mailbox_transaction_context *trans;
	struct mail *mail;
};

/*
 * Forward declarations
 */

static void testsuite_mailstore_free(bool all);

/*
 * State
 */

static struct mail_user *testsuite_mailstore_user = NULL;

static struct testsuite_mailstore_mail *testsuite_mailstore_mail = NULL;

static char *testsuite_mailstore_location = NULL;
static char *testsuite_mailstore_attrs = NULL;

/*
 * Initialization
 */

void testsuite_mailstore_init(void)
{
	struct mail_user *mail_user_dovecot, *mail_user;
	struct mail_namespace *ns;
	struct mail_namespace_settings *ns_set;
	struct mail_storage *storage;
	const char *tmpdir, *error, *cwd;

	tmpdir = testsuite_tmp_dir_get();
	testsuite_mailstore_location =
		i_strconcat(tmpdir, "/mailstore", NULL);
	testsuite_mailstore_attrs =
		i_strconcat(tmpdir, "/mail-attrs.dict", NULL);

	if (mkdir(testsuite_mailstore_location, 0700) < 0) {
		i_fatal("failed to create temporary directory '%s': %m.",
			testsuite_mailstore_location);
	}

	mail_user_dovecot = sieve_tool_get_mail_user(sieve_tool);

	if (t_get_working_dir(&cwd, &error) < 0)
		i_fatal("Failed to get working directory: %s", error);
	const char *const code_override_fields[] = {
		t_strconcat("mail_home=", cwd, NULL),
		"mail_driver=maildir",
		t_strconcat("mail_path=", testsuite_mailstore_location, NULL),
		"mail_attribute/dict=file",
		"mail_attribute/dict/file/driver=file",
		t_strconcat("mail_attribute/dict/file/path=", testsuite_mailstore_attrs, NULL),
		NULL,
	};
	struct settings_instance *set_instance =
		mail_storage_service_user_get_settings_instance(mail_user_dovecot->service_user);
	struct mail_storage_service_input input = {
		.username = "testsuite-mail-user@example.org",
		.set_instance = set_instance,
		.no_userdb_lookup = TRUE,
		.code_override_fields = code_override_fields,
	};
	if (mail_storage_service_lookup_next(
			sieve_tool_get_mail_storage_service(sieve_tool),
			&input, &mail_user, &error) < 0)
		i_fatal("Test user initialization failed: %s", error);
	mail_user->autocreated = TRUE;

	ns_set = p_new(mail_user->pool, struct mail_namespace_settings, 1);
	ns_set->name = "";
	ns_set->separator = ".";

	ns = mail_namespaces_init_empty(mail_user);
	ns->flags |= NAMESPACE_FLAG_INBOX_USER;
	ns->set = ns_set;

	if (mail_storage_create(ns, mail_user->event, 0, &storage, &error) < 0)
		i_fatal("Couldn't create testsuite storage: %s", error);
	if (mail_namespaces_init_finish(ns, &error) < 0)
		i_fatal("Couldn't create testsuite namespace: %s", error);

	testsuite_mailstore_user = mail_user;
}

void testsuite_mailstore_deinit(void)
{
	const char *error;

	testsuite_mailstore_free(TRUE);

	if (unlink_directory(testsuite_mailstore_location,
			     UNLINK_DIRECTORY_FLAG_RMDIR, &error) < 0) {
		i_warning("failed to remove temporary directory '%s': %s.",
			  testsuite_mailstore_location, error);
	}

	i_free(testsuite_mailstore_location);
	i_free(testsuite_mailstore_attrs);
	mail_user_unref(&testsuite_mailstore_user);
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

bool testsuite_mailstore_mailbox_create(
	const struct sieve_runtime_env *renv ATTR_UNUSED, const char *folder)
{
	struct mail_user *mail_user = testsuite_mailstore_user;
	struct mail_namespace *ns = mail_user->namespaces;
	struct mailbox *box;

	box = mailbox_alloc(ns->list, folder, 0);

	if (mailbox_create(box, NULL, FALSE) < 0) {
		mailbox_free(&box);
		return FALSE;
	}

	mailbox_free(&box);

	return TRUE;
}

static struct testsuite_mailstore_mail *
testsuite_mailstore_open(const char *folder)
{
	enum mailbox_flags flags =
		MAILBOX_FLAG_SAVEONLY | MAILBOX_FLAG_POST_SESSION;
	struct mail_user *mail_user = testsuite_mailstore_user;
	struct mail_namespace *ns = mail_user->namespaces;
	struct mailbox *box;
	struct mailbox_transaction_context *t;
	struct testsuite_mailstore_mail *tmail, *tmail_prev;
	const char *error;

	if (!sieve_mailbox_check_name(folder, &error)) {
		e_error(testsuite_sieve_instance->event,
			"testsuite: invalid mailbox name '%s' specified: %s",
			folder, error);
		return NULL;
	}

	tmail = testsuite_mailstore_mail;
	tmail_prev = NULL;
	while (tmail != NULL) {
		if (strcmp(tmail->folder, folder) == 0) {
			if (tmail_prev != NULL) {
				/* Remove it from list if it is not first. */
				tmail_prev->next = tmail->next;
			}
			break;
		}
		tmail_prev = tmail;
		tmail = tmail->next;
	}
	if (tmail != NULL) {
		if (tmail != testsuite_mailstore_mail) {
			/* Bring it to front */
			tmail->next = testsuite_mailstore_mail;
			testsuite_mailstore_mail = tmail;
		}
		return tmail;
	}

	box = mailbox_alloc(ns->list, folder, flags);
	if (mailbox_open(box) < 0) {
		e_error(testsuite_sieve_instance->event,
			"testsuite: failed to open mailbox '%s'", folder);
		mailbox_free(&box);
		return NULL;
	}

	/* Sync mailbox */

	if (mailbox_sync(box, MAILBOX_SYNC_FLAG_FULL_READ) < 0) {
		e_error(testsuite_sieve_instance->event,
			"testsuite: failed to sync mailbox '%s'", folder);
		mailbox_free(&box);
		return NULL;
	}

	/* Start transaction */

	t = mailbox_transaction_begin(box, 0, __func__);

	tmail = i_new(struct testsuite_mailstore_mail, 1);
	tmail->next = testsuite_mailstore_mail;
	testsuite_mailstore_mail = tmail;

	tmail->folder = i_strdup(folder);
	tmail->box = box;
	tmail->trans = t;
	tmail->mail = mail_alloc(t, 0, NULL);

	return tmail;
}

static void testsuite_mailstore_free(bool all)
{
	struct testsuite_mailstore_mail *tmail;

	if (testsuite_mailstore_mail == NULL)
		return;

	tmail = (all ?
		 testsuite_mailstore_mail : testsuite_mailstore_mail->next);
	while (tmail != NULL) {
		struct testsuite_mailstore_mail *tmail_next = tmail->next;

		mail_free(&tmail->mail);
		mailbox_transaction_rollback(&tmail->trans);
		mailbox_free(&tmail->box);
		i_free(tmail->folder);
		i_free(tmail);

		tmail = tmail_next;
	}
	if (all)
		testsuite_mailstore_mail = NULL;
	else
		testsuite_mailstore_mail->next = NULL;
}

void testsuite_mailstore_flush(void)
{
	testsuite_mailstore_free(FALSE);
}

bool testsuite_mailstore_mail_index(const struct sieve_runtime_env *renv,
				    const char *folder, unsigned int index)
{
	struct testsuite_mailstore_mail *tmail;
	struct mailbox_status status;

	tmail = testsuite_mailstore_open(folder);
	if (tmail == NULL)
		return FALSE;

	mailbox_get_open_status(tmail->box, STATUS_MESSAGES, &status);
	if (index >= status.messages)
		return FALSE;

	mail_set_seq(tmail->mail, index+1);
	testsuite_message_set_mail(renv, tmail->mail);

	return TRUE;
}

/*
 * IMAP metadata
 */

int testsuite_mailstore_set_imap_metadata(const char *mailbox,
					  const char *annotation,
					  const char *value)
{
	struct imap_metadata_transaction *imtrans;
	struct mail_attribute_value avalue;
	struct mailbox *box;
	enum mail_error error_code;
	const char *error;
	int ret;

	if (!imap_metadata_verify_entry_name(annotation, &error)) {
		e_error(testsuite_sieve_instance->event,
			"testsuite: imap metadata: "
			"specified annotation name '%s' is invalid: %s",
			str_sanitize(annotation, 256), error);
		return -1;
	}

	if (mailbox != NULL) {
		struct mail_namespace *ns;
		ns = mail_namespace_find(testsuite_mailstore_user->namespaces,
					 mailbox);
		box = mailbox_alloc(ns->list, mailbox, 0);
		imtrans = imap_metadata_transaction_begin(box);
	} else {
		box = NULL;
		imtrans = imap_metadata_transaction_begin_server(
			testsuite_mailstore_user);
	}

	i_zero(&avalue);
	avalue.value = value;
	if ((ret = imap_metadata_set(imtrans, annotation, &avalue)) < 0) {
		error = imap_metadata_transaction_get_last_error(
			imtrans, &error_code);
		imap_metadata_transaction_rollback(&imtrans);
	} else {
		ret = imap_metadata_transaction_commit(&imtrans,
						       &error_code, &error);
	}
	if (box != NULL)
		mailbox_free(&box);

	if (ret < 0) {
		e_error(testsuite_sieve_instance->event,
			"testsuite: imap metadata: "
			"failed to assign annotation '%s': %s",
			str_sanitize(annotation, 256), error);
		return -1;
	}
	return 0;
}
