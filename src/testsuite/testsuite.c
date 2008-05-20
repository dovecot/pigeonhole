/* Copyright (c) 2005-2007 Dovecot authors, see the included COPYING file */

#include "lib.h"
#include "ostream.h"
#include "mail-storage.h"

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

#include "lib.h"
#include "lib-signals.h"
#include "ioloop.h"
#include "ostream.h"
#include "hostpid.h"
#include "mail-storage.h"

#include "sieve.h"
#include "sieve-extensions.h"

#include "testsuite-common.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>

/* Functionality common to all sieve test binaries */

/* FIXME: this file is currently very messy */

static struct ioloop *ioloop;

static void sig_die(int signo, void *context ATTR_UNUSED)
{
	/* warn about being killed because of some signal, except SIGINT (^C)
	   which is too common at least while testing :) */
	if (signo != SIGINT)
		i_warning("Killed with signal %d", signo);
	// io_loop_stop(ioloop); We are not running an ioloop
	exit(1);
}

static void testsuite_init(void) 
{
	lib_init();
	ioloop = io_loop_create();

	lib_signals_init();
	lib_signals_set_handler(SIGINT, TRUE, sig_die, NULL);
	lib_signals_set_handler(SIGTERM, TRUE, sig_die, NULL);
	lib_signals_ignore(SIGPIPE, TRUE);
	lib_signals_ignore(SIGALRM, FALSE);

	if ( !sieve_init("") ) 
		i_fatal("Failed to initialize sieve implementation\n");

	(void) sieve_extension_register(&testsuite_extension);
}

static void testsuite_deinit(void)
{
	sieve_deinit();
	
	lib_signals_deinit();

	io_loop_destroy(&ioloop);
	lib_deinit();
}

static const char *_get_user(void)
{
	uid_t process_euid = geteuid();
	struct passwd *pw = getpwuid(process_euid);
	if (pw != NULL) {
		return t_strdup(pw->pw_name);
	} 
		
	i_fatal("Couldn't lookup our username (uid=%s)", dec2str(process_euid));
	return NULL;
}

static struct sieve_binary *_compile_sieve_script(const char *filename)
{
	struct sieve_error_handler *ehandler;
	struct sieve_binary *sbin;
	
	ehandler = sieve_stderr_ehandler_create();
	sieve_error_handler_accept_infolog(ehandler, TRUE);

	if ( (sbin = sieve_compile(filename, ehandler)) == NULL ) {
		sieve_error_handler_unref(&ehandler);
		i_fatal("Failed to compile sieve script\n");
	}

	sieve_error_handler_unref(&ehandler);
		
	return sbin;
}
		
static void _dump_sieve_binary_to(struct sieve_binary *sbin, const char *filename)	
{
	int dfd = -1;
	struct ostream *dumpstream;
	
	if ( filename == NULL ) return;
	
	if ( strcmp(filename, "-") == 0 ) 
		dumpstream = o_stream_create_fd(1, 0, FALSE);
	else {
		if ( (dfd = open(filename, O_WRONLY | O_CREAT)) < 0 ) {
			i_fatal("Failed to open dump-file for writing: %m");
			exit(1);
		}
		
		dumpstream = o_stream_create_fd(dfd, 0, FALSE);
	}
	
	if ( dumpstream != NULL ) {
		(void) sieve_dump(sbin, dumpstream);
		o_stream_destroy(&dumpstream);
	} else {
		i_fatal("Failed to create stream for sieve code dump.");
	}
	
	if ( dfd != -1 )
		close(dfd);
}

static void _fill_in_envelope
	(struct mail *mail, const char **recipient, const char **sender)
{
	/* Get recipient address */
	if ( *recipient == NULL ) 
		(void)mail_get_first_header(mail, "Envelope-To", recipient);
	if ( *recipient == NULL ) 
		(void)mail_get_first_header(mail, "To", recipient);
	if ( *recipient == NULL ) 
		*recipient = "recipient@example.com";
	
	/* Get sender address */
	if ( *sender == NULL ) 
		(void)mail_get_first_header(mail, "Return-path", sender);
	if ( *sender == NULL ) 
		(void)mail_get_first_header(mail, "Sender", sender);
	if ( *sender == NULL ) 
		(void)mail_get_first_header(mail, "From", sender);
	if ( *sender == NULL ) 
		*sender = "sender@example.com";
}

static void print_help(void)
{
	printf(
"Usage: testsuite [-d <dump filename>] <scriptfile>\n"
	);
}

static const char *message_data = 
"From: stephan@rename-it.nl\n"
"To: sirius@drunksnipers.com\n"
"Subject: Frop!\n"
"\n"
"Friep!\n";

int main(int argc, char **argv) 
{
	const char *scriptfile, *dumpfile, *recipient, *sender; 
	const char *user;
	int i;
	pool_t namespaces_pool;
	struct mail_raw *mailr;
	struct sieve_binary *sbin;
	struct sieve_message_data msgdata;
	struct sieve_script_env scriptenv;
	struct sieve_error_handler *ehandler;
	string_t *mail_buffer;

	testsuite_init();

	mail_buffer = t_str_new(1024);
	str_append(mail_buffer, message_data);

	/* Parse arguments */
	scriptfile = dumpfile = recipient = sender = NULL;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-d") == 0) {
			/* dump file */
			i++;
			if (i == argc)
				i_fatal("Missing -d argument");
			dumpfile = argv[i];
		} else if ( scriptfile == NULL ) {
			scriptfile = argv[i];
		} else {
			print_help();
			i_fatal("Unknown argument: %s", argv[i]);
		}
	}
	
	if ( scriptfile == NULL ) {
		print_help();
		i_fatal("Missing <scriptfile> argument");
	}
	
	/* Compile sieve script */
	sbin = _compile_sieve_script(scriptfile);

	/* Dump script */
	_dump_sieve_binary_to(sbin, dumpfile);
	
	user = _get_user();

	namespaces_pool = namespaces_init();
	mail_raw_init(namespaces_pool, user);
	mailr = mail_raw_open(mail_buffer);

	_fill_in_envelope(mailr->mail, &recipient, &sender);

	/* Collect necessary message data */
	memset(&msgdata, 0, sizeof(msgdata));
	msgdata.mail = mailr->mail;
	msgdata.return_path = recipient;
	msgdata.to_address = sender;
	msgdata.auth_user = user;
	(void)mail_get_first_header(mailr->mail, "Message-ID", &msgdata.id);

	memset(&scriptenv, 0, sizeof(scriptenv));
	scriptenv.inbox = "INBOX";
	scriptenv.username = user;

	ehandler = sieve_stderr_ehandler_create();	
	
	/* Run the test */
	(void) sieve_test(sbin, &msgdata, &scriptenv, ehandler);

	sieve_close(&sbin);
	sieve_error_handler_unref(&ehandler);

	mail_raw_close(mailr);
	mail_raw_deinit();
	namespaces_deinit();

	testsuite_deinit();  

	return 0;
}
