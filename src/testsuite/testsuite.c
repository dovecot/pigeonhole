/* Copyright (c) 2005-2007 Dovecot authors, see the included COPYING file */

#include "lib.h"
#include "lib-signals.h"
#include "ioloop.h"
#include "ostream.h"
#include "hostpid.h"
#include "mail-storage.h"
#include "env-util.h"

#include "mail-raw.h"
#include "namespaces.h"

#include "sieve.h"
#include "sieve-extensions.h"
#include "sieve-script.h"
#include "sieve-binary.h"
#include "sieve-result.h"
#include "sieve-interpreter.h"

#include "testsuite-common.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>

#define DEFAULT_SENDMAIL_PATH "/usr/lib/sendmail"
#define DEFAULT_ENVELOPE_SENDER "MAILER-DAEMON"

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

static void testsuite_bin_init(void) 
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
	
	testsuite_init();
}
static void testsuite_bin_deinit(void)
{
	testsuite_deinit();
	
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
	
	ehandler = sieve_stderr_ehandler_create(0);
	sieve_error_handler_accept_infolog(ehandler, TRUE);

	if ( (sbin = sieve_compile(filename, ehandler)) == NULL ) {
		sieve_error_handler_unref(&ehandler);
		i_fatal("Failed to compile test script %s\n", filename);
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

static void print_help(void)
{
	printf(
"Usage: testsuite [-d <dump filename>] <scriptfile>\n"
	);
}

static int testsuite_run
(struct sieve_binary *sbin, const struct sieve_script_env *scriptenv,
	bool trace)
{
	struct sieve_error_handler *ehandler = sieve_stderr_ehandler_create(0);
	struct sieve_result *sres = sieve_result_create(ehandler);	
	struct sieve_interpreter *interp;
	int ret = 0;

	if ( trace ) {
		struct ostream *tstream = o_stream_create_fd(1, 0, FALSE);
		
		interp=sieve_interpreter_create(sbin, ehandler, tstream);
		
	    ret = sieve_interpreter_run(interp, &testsuite_msgdata, scriptenv, &sres);
	
		o_stream_destroy(&tstream);
	} else {
		interp=sieve_interpreter_create(sbin, ehandler, NULL);

	    ret = sieve_interpreter_run(interp, &testsuite_msgdata, scriptenv, &sres);
	}

	sieve_interpreter_free(&interp);
	sieve_result_unref(&sres);

	sieve_error_handler_unref(&ehandler);

	return ret;	
}

int main(int argc, char **argv) 
{
	const char *scriptfile, *dumpfile; 
	const char *user;
	int i, ret;
	struct sieve_binary *sbin;
	const char *sieve_dir;
	struct sieve_script_env scriptenv;
	bool trace = FALSE;

	testsuite_bin_init();

	/* Parse arguments */
	scriptfile = dumpfile =  NULL;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-d") == 0) {
			/* dump file */
			i++;
			if (i == argc)
				i_fatal("Missing -d argument");
			dumpfile = argv[i];
#ifdef SIEVE_RUNTIME_TRACE
		} else if (strcmp(argv[i], "-t") == 0) {
			/* runtime trace */
			trace = TRUE;
#endif
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

	printf("Test case: %s:\n\n", scriptfile);

	/* Initialize environment */
	sieve_dir = strrchr(scriptfile, '/');
    if ( sieve_dir == NULL )
        sieve_dir= "./";
	else
		sieve_dir = t_strdup_until(scriptfile, sieve_dir+1);

	/* Currently needed for include (FIXME) */
	env_put(t_strconcat("SIEVE_DIR=", sieve_dir, "included", NULL));
	env_put(t_strconcat("SIEVE_GLOBAL_DIR=", sieve_dir, "included-global", NULL));
	
	/* Compile sieve script */
	sbin = _compile_sieve_script(scriptfile);

	/* Dump script */
	_dump_sieve_binary_to(sbin, dumpfile);
	
	namespaces_init();
	user = _get_user();
	testsuite_message_init(user);

	memset(&scriptenv, 0, sizeof(scriptenv));
	scriptenv.inbox = "INBOX";
	scriptenv.username = user;

	/* Run the test */
	ret = testsuite_run(sbin, &scriptenv, trace);

	switch ( ret ) {
	case SIEVE_EXEC_OK:
		break;
	case SIEVE_EXEC_FAILURE:
	case SIEVE_EXEC_KEEP_FAILED:
		testsuite_testcase_fail("execution aborted");
		break;
	case SIEVE_EXEC_BIN_CORRUPT:
        testsuite_testcase_fail("binary corrupt");
		break;
	default:
		testsuite_testcase_fail("unknown execution exit code");
	}

	sieve_close(&sbin);

	testsuite_message_deinit();
	namespaces_deinit();

	testsuite_bin_deinit();  

	return testsuite_testcase_result();
}
