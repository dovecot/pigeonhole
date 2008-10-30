/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "ostream.h"
#include "mail-storage.h"
#include "mail-namespace.h"
#include "env-util.h"

#include "bin-common.h"
#include "mail-raw.h"
#include "sieve.h"
#include "sieve-binary.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>

#define DEFAULT_SENDMAIL_PATH "/usr/lib/sendmail"
#define DEFAULT_ENVELOPE_SENDER "MAILER-DAEMON"

/*
 * Dummy SMTP session
 */

static void *sieve_smtp_open(const char *destination,
	const char *return_path, FILE **file_r)
{
	i_info("sending mesage from <%s> to <%s>:",
		return_path == NULL || *return_path == '\0' ? "" : return_path, 
		destination);
	printf("\nSTART MESSAGE:\n");
	
	*file_r = stdout;
	
	return NULL;	
}

static bool sieve_smtp_close(void *handle ATTR_UNUSED)
{
	printf("END MESSAGE\n\n");
	return TRUE;
}

/*
 * Dummy duplicate check implementation
 */

static int duplicate_check(const void *id ATTR_UNUSED, size_t id_size ATTR_UNUSED, 
	const char *user)
{
	i_info("checked duplicate for user %s.\n", user);
	return 0;
}

static void duplicate_mark
(const void *id ATTR_UNUSED, size_t id_size ATTR_UNUSED, const char *user, 
	time_t time ATTR_UNUSED)
{
	i_info("marked duplicate for user %s.\n", user);
}

/*
 * Print help 
 */

static void print_help(void)
{
	printf(
"Usage: sieve-exec [-r <recipient address>][-s <envelope sender>]\n"
"                  [-m <mailbox>][-d <dump filename>][-l <mail location>]\n"
"                  <scriptfile> <mailfile>\n"
	);
}

/*
 * Tool implementation
 */

int main(int argc, char **argv) 
{
	const char *scriptfile, *recipient, *sender, *mailbox, *dumpfile, *mailfile;
	const char *mailloc; 
	const char *user, *home;
	int i;
	struct mail_raw *mailr;
	struct mail_namespace *ns = NULL;
	struct mail_user *mail_user = NULL;
	struct sieve_binary *sbin;
	struct sieve_message_data msgdata;
	struct sieve_script_env scriptenv;
	struct sieve_exec_status estatus;
	struct sieve_error_handler *ehandler;

	bin_init();

	/* Parse arguments */
	scriptfile = recipient = sender = mailbox = dumpfile = mailfile = NULL;
	mailloc = NULL;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-r") == 0) {
			/* recipient address */
			i++;
			if (i == argc)
				i_fatal("Missing -r argument");
			recipient = argv[i];
		} else if (strcmp(argv[i], "-s") == 0) {
			/* envelope sender */
			i++;
			if (i == argc)
				i_fatal("Missing -s argument");
			sender = argv[i];
		} else if (strcmp(argv[i], "-m") == 0) {
			/* default mailbox (keep box) */
			i++;
			if (i == argc) 
				i_fatal("Missing -m argument");
			mailbox = argv[i];
		} else if (strcmp(argv[i], "-d") == 0) {
			/* dump file */
			i++;
			if (i == argc)
				i_fatal("Missing -d argument");
			dumpfile = argv[i];
		} else if (strcmp(argv[i], "-l") == 0) {
			/* mail location */
			i++;
			if (i == argc)
				i_fatal("Missing -l argument");
			mailloc = argv[i];
		} else if ( scriptfile == NULL ) {
			scriptfile = argv[i];
		} else if ( mailfile == NULL ) {
			mailfile = argv[i];
		} else {
			print_help();
			i_fatal("Unknown argument: %s", argv[i]);
		}
	}
	
	if ( scriptfile == NULL ) {
		print_help();
		i_fatal("Missing <scriptfile> argument");
	}
	
	if ( mailfile == NULL ) {
		print_help();
		i_fatal("Missing <mailfile> argument");
	}
	
	/* Compile sieve script */
	sbin = bin_open_sieve_script(scriptfile);
	
	/* Dump script */
	bin_dump_sieve_binary_to(sbin, dumpfile);
	
	user = bin_get_user();
	home = getenv("HOME");

	/* Initialize mail storages */
	mail_storage_init();
	mail_storage_register_all();
	mailbox_list_register_all();

	/* Obtain mail namespaces from -l argument */
	if ( mailloc != NULL ) {
		env_put(t_strdup_printf("NAMESPACE_1=%s", mailloc));
		env_put("NAMESPACE_1_INBOX=1");
		env_put("NAMESPACE_1_LIST=1");
		env_put("NAMESPACE_1_SEP=.");
		env_put("NAMESPACE_1_SUBSCRIPTIONS=1");

		mail_user = mail_user_init(user, home);
	    if (mail_namespaces_init(mail_user) < 0)
    	    i_fatal("Namespace initialization failed");	

		ns = mail_user->namespaces;
	} 

	/* Initialize raw mail object from file */
	mail_raw_init(user);
	mailr = mail_raw_open_file(mailfile);

	bin_fill_in_envelope(mailr->mail, &recipient, &sender);

	if ( mailbox == NULL )
		mailbox = "INBOX";
		
	/* Collect necessary message data */
	memset(&msgdata, 0, sizeof(msgdata));
	msgdata.mail = mailr->mail;
	msgdata.return_path = sender;
	msgdata.to_address = recipient;
	msgdata.auth_user = "nico";
	(void)mail_get_first_header(mailr->mail, "Message-ID", &msgdata.id);
	
	memset(&scriptenv, 0, sizeof(scriptenv));
	scriptenv.default_mailbox = "INBOX";
	scriptenv.namespaces = ns;
	scriptenv.username = user;
	scriptenv.hostname = "host.example.com";
	scriptenv.postmaster_address = "postmaster@example.com";
	scriptenv.smtp_open = sieve_smtp_open;
	scriptenv.smtp_close = sieve_smtp_close;
	scriptenv.duplicate_mark = duplicate_mark;
	scriptenv.duplicate_check = duplicate_check;
	
	ehandler = sieve_stderr_ehandler_create(0);
	sieve_error_handler_accept_infolog(ehandler, TRUE);

	/* Run */
	switch ( sieve_execute(sbin, &msgdata, &scriptenv, &estatus, ehandler, NULL) ) {
	case SIEVE_EXEC_OK:
		i_info("Final result: success");
		break;
	case SIEVE_EXEC_FAILURE:
		i_info("Final result: failed; resolved with successful implicit keep");
		break;
	case SIEVE_EXEC_BIN_CORRUPT:
		i_info("Corrupt binary deleted.");
		(void) unlink(sieve_binary_path(sbin));
	case SIEVE_EXEC_KEEP_FAILED:
		i_info("Final result: utter failure (caller please handle implicit keep!)");
		break;
	default:
		i_info("Final result: unrecognized return value?!");	
	}

	sieve_close(&sbin);
	sieve_error_handler_unref(&ehandler);
	
	/* De-initialize raw mail object */
	mail_raw_close(mailr);
	mail_raw_deinit();

	/* De-initialize mail user object */
	if ( mail_user != NULL )
		mail_user_deinit(&mail_user);

	/* De-intialize mail storages */
	mail_storage_deinit();

	bin_deinit();  
	return 0;
}

