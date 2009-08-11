/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "mempool.h"
#include "imem.h"
#include "array.h"
#include "strfuncs.h"
#include "unlink-directory.h"
#include "env-util.h"
#include "mail-namespace.h"
#include "mail-storage.h"

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

static char *testsuite_mailstore_tmp = NULL;
static struct mail_user *testsuite_mailstore_user = NULL;

static char *testsuite_mailstore_folder = NULL;
static struct mailbox *testsuite_mailstore_box = NULL;
static struct mailbox_transaction_context *testsuite_mailstore_trans = NULL;
static struct mail *testsuite_mailstore_mail = NULL;

/*
 * Initialization
 */

void testsuite_mailstore_init
(const char *user, const char *home, struct mail_user *service_user)
{	
	struct mail_namespace_settings ns_set;
	struct mail_namespace *ns = NULL;
	struct mail_user *mail_user;
	const char *errstr;

	testsuite_mailstore_tmp = i_strconcat
		(testsuite_tmp_dir_get(), "/mailstore", NULL);

	if ( mkdir(testsuite_mailstore_tmp, 0700) < 0 ) {
		i_fatal("failed to create temporary directory '%s': %m.", 
			testsuite_mailstore_tmp);		
	}

	mail_user = mail_user_alloc(user, service_user->unexpanded_set);
	mail_user_set_home(mail_user, home);

	if (mail_user_init(mail_user, &errstr) < 0)
		i_fatal("Test user initialization failed: %s", errstr);

	memset(&ns_set, 0, sizeof(ns_set));
	ns_set.location = t_strconcat("maildir:", testsuite_mailstore_tmp, NULL);
	//ns_set.inbox = TRUE;
	//ns_set.separator = ".";
	//ns_set.subscriptions = TRUE;

	ns = mail_namespaces_init_empty(mail_user);
	ns->flags |= NAMESPACE_FLAG_NOQUOTA | NAMESPACE_FLAG_NOACL;
	ns->set = &ns_set;

	testsuite_mailstore_user = mail_user;
}

void testsuite_mailstore_deinit(void)
{
	testsuite_mailstore_close();

	/* De-initialize mail user object */
	if ( testsuite_mailstore_user != NULL )
		mail_user_unref(&testsuite_mailstore_user);

	if ( unlink_directory(testsuite_mailstore_tmp, TRUE) < 0 ) {
		i_warning("failed to remove temporary directory '%s': %m.",
			testsuite_mailstore_tmp);
	}
	
	i_free(testsuite_mailstore_tmp);		
}

void testsuite_mailstore_reset(void)
{
}

/*
 * Mailbox Access
 */

struct mail_namespace *testsuite_mailstore_get_namespace(void)
{
	return testsuite_mailstore_user->namespaces;
}

bool testsuite_mailstore_mailbox_create
(const struct sieve_runtime_env *renv ATTR_UNUSED, const char *folder)
{
	struct mail_namespace *ns = testsuite_mailstore_user->namespaces;
	struct mailbox *box;

	box = mailbox_alloc(ns->list, folder, NULL, 0);

	if ( mailbox_create(box, NULL, FALSE) < 0 ) {
		mailbox_close(&box);
		return FALSE;
	}

	mailbox_close(&box);

	return TRUE;
}

static void testsuite_mailstore_close(void)
{
	if ( testsuite_mailstore_mail != NULL )
		mail_free(&testsuite_mailstore_mail);

	if ( testsuite_mailstore_trans != NULL )
		mailbox_transaction_rollback(&testsuite_mailstore_trans);
		
	if ( testsuite_mailstore_box != NULL )
		mailbox_close(&testsuite_mailstore_box);

	if ( testsuite_mailstore_folder != NULL )
		i_free(testsuite_mailstore_folder);
}

static struct mail *testsuite_mailstore_open(const char *folder)
{
	enum mailbox_flags flags =
		MAILBOX_FLAG_KEEP_RECENT | MAILBOX_FLAG_SAVEONLY |
		MAILBOX_FLAG_POST_SESSION;
	struct mail_namespace *ns = testsuite_mailstore_user->namespaces;
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

	box = mailbox_alloc(ns->list, folder, NULL, flags);
	if ( mailbox_open(box) < 0 ) {
		sieve_sys_error("testsuite: failed to open mailbox '%s'", folder);
		return NULL;	
	}
	
	/* Sync mailbox */

	if ( mailbox_sync(box, MAILBOX_SYNC_FLAG_FULL_READ, 0, NULL) < 0 ) {
		sieve_sys_error("testsuite: failed to sync mailbox '%s'", folder);
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

	mail_set_seq(mail, index);
	testsuite_message_set_mail(renv, mail);

	return TRUE;
}
