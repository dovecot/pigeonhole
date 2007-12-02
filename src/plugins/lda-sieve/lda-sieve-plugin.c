/* Copyright (c) 2002-2007 Dovecot authors, see the included COPYING file */

#include "lib.h"
#include "home-expand.h"
#include "deliver.h"
#include "duplicate.h"
#include "smtp-client.h"
#include "sieve.h"

#include "lda-sieve-plugin.h"

#include <stdlib.h>
#include <sys/stat.h>

#define SIEVE_SCRIPT_PATH "~/.dovecot.sieve"

static deliver_mail_func_t *next_deliver_mail;

static const char *lda_sieve_get_path(void)
{
	const char *script_path, *home;
	struct stat st;

	home = getenv("HOME");

	/* userdb may specify Sieve path */
	script_path = getenv("SIEVE");
	if (script_path != NULL) {
		if (*script_path == '\0') {
			/* disabled */
			return NULL;
		}

		if (*script_path != '/' && *script_path != '\0') {
			/* relative path. change to absolute. */
			script_path = t_strconcat(getenv("HOME"), "/",
						  script_path, NULL);
		}
	} else {
		if (home == NULL) {
			i_error("Per-user script path is unknown. See "
				"http://wiki.dovecot.org/LDA/Sieve#location");
			return NULL;
		}

		script_path = home_expand(SIEVE_SCRIPT_PATH);
	}

	if (stat(script_path, &st) < 0) {
		if (errno != ENOENT)
			i_error("stat(%s) failed: %m", script_path);

		/* use global script instead, if one exists */
		script_path = getenv("SIEVE_GLOBAL_PATH");
		if (script_path == NULL) {
			/* for backwards compatibility */
			script_path = getenv("GLOBAL_SCRIPT_PATH");
		}
	}

	return script_path;
}

static void *lda_sieve_smtp_open(const char *destination,
	const char *return_path, FILE **file_r)
{
	return (void *) smtp_client_open(destination, return_path, file_r);
}

static bool lda_sieve_smtp_close(void *handle)
{
	struct smtp_client *smtp_client = (struct smtp_client *) handle;
	
	return ( smtp_client_close(smtp_client) >= 0 );
}

static int lda_sieve_run
(struct mail_namespace *namespaces, struct mail *mail, const char *script_path,
	const char *destaddr, const char *username, const char *mailbox)
{
	bool debug = ( getenv("DEBUG") != NULL );
	struct sieve_message_data msgdata;
	struct sieve_mail_environment mailenv;
	struct sieve_binary *sbin;
	
	if ( debug )
		i_info("lda-sieve: Compiling script %s", script_path);
	
	if ( (sbin=sieve_compile(script_path)) == NULL )
		return -1;

	/* Collect necessary message data */
	memset(&msgdata, 0, sizeof(msgdata));
	msgdata.mail = mail;
	msgdata.return_path = deliver_get_return_address(mail);
	msgdata.to_address = destaddr;
	msgdata.auth_user = username;
	(void)mail_get_first_header(mail, "Message-ID", &msgdata.id);
	
	memset(&mailenv, 0, sizeof(mailenv));
	mailenv.inbox = mailbox;
	mailenv.namespaces = namespaces;
	mailenv.username = username;
	mailenv.hostname = deliver_set->hostname;
	mailenv.postmaster_address = deliver_set->postmaster_address;
	mailenv.smtp_open = lda_sieve_smtp_open;
	mailenv.smtp_close = lda_sieve_smtp_close;
	mailenv.duplicate_mark = duplicate_mark;
	mailenv.duplicate_check = duplicate_check;

	if ( debug )
		i_info("lda-sieve: Executing (in-memory) script %s", script_path);
	
	if ( sieve_execute(sbin, &msgdata, &mailenv) )
		return 1;
		
	return -1;
}

static int lda_sieve_deliver_mail
(struct mail_namespace *namespaces, struct mail_storage **storage_r ATTR_UNUSED, 
	struct mail *mail, const char *destaddr, const char *mailbox)
{
	const char *script_path;

	script_path = lda_sieve_get_path();
	if (script_path == NULL)
		return 0;

	if (getenv("DEBUG") != NULL)
		i_info("sieve: Using sieve path: %s", script_path);

	return lda_sieve_run(namespaces, mail, script_path,
			     destaddr, getenv("USER"), mailbox);
}

void sieve_plugin_init(void)
{
	next_deliver_mail = deliver_mail;
	deliver_mail = lda_sieve_deliver_mail;
}

void sieve_plugin_deinit(void)
{
	deliver_mail = next_deliver_mail;
}
