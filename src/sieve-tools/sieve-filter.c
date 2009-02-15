/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "ostream.h"
#include "array.h"
#include "mail-namespace.h"
#include "mail-storage.h"
#include "mail-search-build.h"
#include "env-util.h"

#include "sieve.h"
#include "sieve-binary.h"

#include "mail-raw.h"
#include "sieve-tool.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>

/*
 * Print help
 */

static void print_help(void)
{
	printf(
"Usage: sieve-filter [-m <mailbox>] [-x <extensions>]\n"
"                    [-s <scriptfile>] [-c]\n"
"                    <scriptfile> <mailstore>\n"
	);
}

static int filter_mailbox(struct mailbox *box)
{
	struct mail_search_args *search_args;
	struct mailbox_transaction_context *t;
	struct mail_search_context *search_ctx;
	struct mail *mail;

	search_args = mail_search_build_init();
	mail_search_build_add_all(search_args);

	if ( mailbox_sync(box, MAILBOX_SYNC_FLAG_FAST, 0, NULL) < 0 ) {
		i_fatal("sync failed");
	}
		
	t = mailbox_transaction_begin(box, 0);
	search_ctx = mailbox_search_init(t, search_args, NULL);
	mail_search_args_unref(&search_args);

	mail = mail_alloc(t, 0, NULL);
	while ( mailbox_search_next(search_ctx, mail) > 0 ) {
		const char *subject, *date;
		uoff_t size = 0;
		
		if ( mail->expunged )
			continue;
			
		if ( mail_get_virtual_size(mail, &size) < 0 )
			i_fatal("failed to get size");
		
		(void)mail_get_first_header(mail, "date", &date);
		(void)mail_get_first_header(mail, "subject", &subject);
		
		printf("MAIL: [%s; %"PRIuUOFF_T" bytes] %s\n", date, size, subject);
	
		/* FIXME: apply Sieve filter here */
	}
	mail_free(&mail);
	
	if ( mailbox_search_deinit(&search_ctx) < 0 ) {
		i_error("failed to deinit search");
	}

	if ( mailbox_transaction_commit(&t) < 0 ) {
		i_fatal("failed to commit transaction");
	}
	
	if ( mailbox_sync(box, MAILBOX_SYNC_FLAG_FAST, 0, NULL) < 0 ) {
		i_fatal("sync failed");
	}
	
	return FALSE;
}

/*
 * Tool implementation
 */

int main(int argc, char **argv) 
{
	const char *scriptfile, *recipient, *sender, *mailbox, *mailstore, 
		*extensions;
	bool force_compile;
	struct mail_namespace *ns = NULL;
	struct mail_user *mail_user = NULL;
	struct mail_storage *storage;
	struct mailbox *box;
	enum mail_error error;
	enum mailbox_open_flags open_flags = 
		MAILBOX_OPEN_KEEP_RECENT | MAILBOX_OPEN_IGNORE_ACLS;
	const char *user, *home;
	int i;

	sieve_tool_init();
	
	/* Parse arguments */
	scriptfile = recipient = sender = mailstore = extensions = NULL;
	mailbox = "INBOX";
	force_compile = FALSE;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-m") == 0) {
			/* default mailbox (keep box) */
			i++;
			if (i == argc) 
				i_fatal("Missing -m argument");
			mailbox = argv[i];
		} else if (strcmp(argv[i], "-x") == 0) {
			/* extensions */
			i++;
			if (i == argc)
				i_fatal("Missing -x argument");
			extensions = argv[i];
		} else if (strcmp(argv[i], "-c") == 0) {
			/* force compile */
			force_compile = TRUE;
		} else if ( scriptfile == NULL ) {
			scriptfile = argv[i];
		} else if ( mailstore == NULL ) {
			mailstore = argv[i];
		} else {
			print_help();
			i_fatal("Unknown argument: %s", argv[i]);
		}
	}
	
	if ( scriptfile == NULL ) {
		print_help();
		i_fatal("Missing <scriptfile> argument");
	}
	
	if ( mailstore == NULL ) {
		print_help();
		i_fatal("Missing <mailfile> argument");
	}

	if ( extensions != NULL ) {
		sieve_set_extensions(extensions);
	}
	
	user = sieve_tool_get_user();
	home = getenv("HOME");

	/* Initialize mail storages */
	mail_users_init(getenv("AUTH_SOCKET_PATH"), getenv("DEBUG") != NULL);
	mail_storage_init();
	mail_storage_register_all();
	mailbox_list_register_all();

	/* Obtain mail namespaces from -l argument */
	if ( mailstore != NULL ) {
		env_put(t_strdup_printf("NAMESPACE_1=%s", mailstore));
		env_put("NAMESPACE_1_INBOX=1");
		env_put("NAMESPACE_1_LIST=1");
		env_put("NAMESPACE_1_SEP=.");
		env_put("NAMESPACE_1_SUBSCRIPTIONS=1");

		mail_user = mail_user_init(user);
		mail_user_set_home(mail_user, home);
		if (mail_namespaces_init(mail_user) < 0)
			i_fatal("Namespace initialization failed");	

		ns = mail_user->namespaces;
	}

	storage = ns->storage;

	box = mailbox_open(&storage, mailbox, NULL, open_flags);
	if ( box == NULL ) {
		i_fatal("Couldn't open mailbox '%s': %s", 
				mailbox, mail_storage_get_last_error(storage, &error));
	}

	filter_mailbox(box);
	
	if ( box != NULL )
		mailbox_close(&box);

	/* De-initialize mail user object */
	if ( mail_user != NULL )
		mail_user_unref(&mail_user);

	/* De-initialize mail storages */
	mail_storage_deinit();
	mail_users_deinit();	
	
	sieve_tool_deinit();
	
	return 0;
}
