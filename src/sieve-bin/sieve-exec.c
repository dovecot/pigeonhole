/* Copyright (c) 2005-2007 Dovecot authors, see the included COPYING file */

#include "lib.h"
#include "ostream.h"
#include "mail-storage.h"
#include "mail-namespace.h"
#include "env-util.h"

#include "bin-common.h"
#include "mail-raw.h"
#include "namespaces.h"
#include "sieve.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>


#define DEFAULT_SENDMAIL_PATH "/usr/lib/sendmail"
#define DEFAULT_ENVELOPE_SENDER "MAILER-DAEMON"

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

static void print_help(void)
{
	printf(
"Usage: sieve-exec [-r <recipient address>][-s <envelope sender>]\n"
"                  [-m <mailbox>][-d <dump filename>][-l <mail location>]\n"
"                  <scriptfile> <mailfile>\n"
	);
}

int main(int argc, char **argv) 
{
	const char *scriptfile, *recipient, *sender, *mailbox, *dumpfile, *mailfile;
	const char *mailloc; 
	const char *user;
	int i, mfd;
	pool_t namespaces_pool;
	struct mail_namespace *ns;
	struct mail_raw *mailr;
	struct sieve_binary *sbin;
	struct sieve_message_data msgdata;
	struct sieve_script_env scriptenv;
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

	/* Open the mail file */
	mfd = bin_open_mail_file(mailfile);
	
	/* Compile sieve script */
	sbin = bin_open_sieve_script(scriptfile);
	
	/* Dump script */
	bin_dump_sieve_binary_to(sbin, dumpfile);
	
	user = bin_get_user();
	namespaces_pool = namespaces_init();

	if ( mailloc != NULL ) {
		env_put(t_strdup_printf("NAMESPACE_1=%s", mailloc));
		env_put("NAMESPACE_1_INBOX=1");
		env_put("NAMESPACE_1_LIST=1");
		env_put("NAMESPACE_1_SEP=.");
		env_put("NAMESPACE_1_SUBSCRIPTIONS=1");
	
		if (mail_namespaces_init(namespaces_pool, user, &ns) < 0)
			i_fatal("Namespace initialization failed");
	} else {
		ns = NULL;
	}

	mail_raw_init(namespaces_pool, user);
	mailr = mail_raw_open(mfd);

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
	scriptenv.inbox = "INBOX";
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
	switch ( sieve_execute(sbin, &msgdata, &scriptenv, ehandler, NULL) ) {
	case SIEVE_EXEC_OK:
		i_info("Final result: success\n");
		break;
	case SIEVE_EXEC_FAILURE:
		i_info("Final result: failed; resolved with successful implicit keep\n");
		break;
	case SIEVE_EXEC_BIN_CORRUPT:
	case SIEVE_EXEC_KEEP_FAILED:
		i_info("Final result: utter failure (caller please handle implicit keep!)\n");
		break;
	default:
		i_info("Final result: unrecognized return value?!\n");	
	}

	sieve_close(&sbin);
	sieve_error_handler_unref(&ehandler);

	bin_close_mail_file(mfd);
	mail_raw_close(mailr);
	mail_raw_deinit();
	namespaces_deinit();

	bin_deinit();  
	return 0;
}

