/* Copyright (c) 2005-2007 Dovecot authors, see the included COPYING file */

#include "lib.h"
#include "ostream.h"
#include "mail-storage.h"

#include "bin-common.h"
#include "mail-raw.h"
#include "namespaces.h"
#include "sieve.h"
#include "sieve-binary.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>

#define DEFAULT_SENDMAIL_PATH "/usr/lib/sendmail"
#define DEFAULT_ENVELOPE_SENDER "MAILER-DAEMON"

static void print_help(void)
{
#ifdef SIEVE_RUNTIME_TRACE
#  define SVTRACE "[-t]"
#else
#  define SVTRACE
#endif
	printf(
"Usage: sieve-test [-r <recipient address>][-s <envelope sender>]\n"
"                  [-m <mailbox>][-d <dump filename>][-c]" SVTRACE "\n"
"                  <scriptfile> <mailfile>\n"
	);
}

int main(int argc, char **argv) 
{
	const char *scriptfile, *recipient, *sender, *mailbox, *dumpfile, *mailfile; 
	const char *user;
	int i, mfd;
	pool_t namespaces_pool;
	struct mail_raw *mailr;
	struct sieve_binary *sbin;
	struct sieve_message_data msgdata;
	struct sieve_script_env scriptenv;
	struct sieve_error_handler *ehandler;
	struct ostream *teststream;
	bool force_compile = FALSE;
	int ret;

#ifdef SIEVE_RUNTIME_TRACE
	bool trace = FALSE;
	struct ostream *trace_stream = FALSE;
#endif

	bin_init();

	/* Parse arguments */
	scriptfile = recipient = sender = mailbox = dumpfile = mailfile = NULL;
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
		} else if (strcmp(argv[i], "-c") == 0) {
            /* force compile */
			force_compile = TRUE;
#ifdef SIEVE_RUNTIME_TRACE
		} else if (strcmp(argv[i], "-t") == 0) {
            /* runtime trace */
			trace = TRUE;
#endif
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
	if ( force_compile ) {
		sbin = bin_compile_sieve_script(scriptfile);
		(void) sieve_save(sbin, NULL);
	} else {
		sbin = bin_open_sieve_script(scriptfile);
	}

	/* Dump script */
	bin_dump_sieve_binary_to(sbin, dumpfile);
	
	user = bin_get_user();

	namespaces_pool = namespaces_init();
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
	msgdata.auth_user = user;
	(void)mail_get_first_header(mailr->mail, "Message-ID", &msgdata.id);

	memset(&scriptenv, 0, sizeof(scriptenv));
	scriptenv.inbox = "INBOX";
	scriptenv.username = user;

	ehandler = sieve_stderr_ehandler_create(0);	

	teststream = o_stream_create_fd(1, 0, FALSE);	

#ifdef SIEVE_RUNTIME_TRACE
	if ( trace )
		trace_stream = teststream;
	else
		trace_stream = NULL;
#endif

	/* Run the test */
	ret = sieve_test
		(sbin, &msgdata, &scriptenv, teststream, ehandler, trace_stream);

	if ( ret == SIEVE_EXEC_BIN_CORRUPT ) {
		i_info("Corrupt binary deleted.");
		(void) unlink(sieve_binary_path(sbin));		
	}

	o_stream_destroy(&teststream);

	sieve_close(&sbin);
	sieve_error_handler_unref(&ehandler);

	bin_close_mail_file(mfd);
	
	mail_raw_close(mailr);
	mail_raw_deinit();
	namespaces_deinit();

	bin_deinit();  
	return 0;
}
