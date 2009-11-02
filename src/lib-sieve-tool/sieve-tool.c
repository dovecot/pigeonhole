/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "lib-signals.h"
#include "ioloop.h"
#include "ostream.h"
#include "hostpid.h"
#include "mail-storage.h"

#include "sieve.h"
#include "sieve-tool.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>

/*
 * Global state
 */

static struct ioloop *ioloop;

/* Sieve instance */

struct sieve_instance *sieve_instance;

static const struct sieve_callbacks sieve_callbacks = {
	NULL
};

/*
 * Signal handlers
 */

static void sig_die(const siginfo_t *si, void *context ATTR_UNUSED)
{
	/* warn about being killed because of some signal, except SIGINT (^C)
	 * which is too common at least while testing :) 
	 */
	if (si->si_signo != SIGINT) {
		/* FIXME: strange error for a command line tool */
		i_warning("Killed with signal %d (by pid=%s uid=%s code=%s)",
 			si->si_signo, dec2str(si->si_pid),
			dec2str(si->si_uid),
			lib_signal_code_to_str(si->si_signo, si->si_code));
	}
	io_loop_stop(current_ioloop);
}

/*
 * Initialization
 */

/* HACK */
static bool _init_lib = FALSE;

void sieve_tool_init(sieve_settings_func_t settings_func, bool init_lib) 
{
	_init_lib = init_lib;

	if ( _init_lib ) {
		lib_init();

		ioloop = io_loop_create();

		lib_signals_init();
		lib_signals_set_handler(SIGINT, TRUE, sig_die, NULL);
		lib_signals_set_handler(SIGTERM, TRUE, sig_die, NULL);
		lib_signals_ignore(SIGPIPE, TRUE);
		lib_signals_ignore(SIGALRM, FALSE);
	}

	if ( (sieve_instance=sieve_init(&sieve_callbacks, NULL)) == NULL )
		i_fatal("failed to initialize sieve implementation\n");
}

void sieve_tool_deinit(void)
{
	sieve_deinit();
	sieve_deinit(&sieve_instance);

	if ( _init_lib ) {
		lib_signals_deinit();
	
		io_loop_destroy(&ioloop);

		lib_deinit();
	}
}


/*
 * Commonly needed functionality
 */

const char *sieve_tool_get_user(void)
{
	const char *user;
	uid_t process_euid;
	struct passwd *pw;

	user = getenv("USER");

	if ( user == NULL || *user == '\0' ) {
		process_euid = geteuid();

		if ((pw = getpwuid(process_euid)) != NULL) {
			user = t_strdup(pw->pw_name);
		}

		if ( user == NULL || *user == '\0' ) {
			i_fatal("couldn't lookup our username (uid=%s)", dec2str(process_euid));
		}
	}
	
	return user;
}

void sieve_tool_get_envelope_data
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

/*
 * Sieve script handling
 */

struct sieve_binary *sieve_tool_script_compile
(const char *filename, const char *name)
{
	struct sieve_error_handler *ehandler;
	struct sieve_binary *sbin;
	
	ehandler = sieve_stderr_ehandler_create(0);
	sieve_error_handler_accept_infolog(ehandler, TRUE);

	if ( (sbin = sieve_compile(sieve_instance, filename, name, ehandler)) == NULL )
		i_error("failed to compile sieve script '%s'\n", filename);

	sieve_error_handler_unref(&ehandler);
		
	return sbin;
}
	
struct sieve_binary *sieve_tool_script_open(const char *filename)
{
	struct sieve_error_handler *ehandler;
	struct sieve_binary *sbin;
	
	ehandler = sieve_stderr_ehandler_create(0);
	sieve_error_handler_accept_infolog(ehandler, TRUE);

	if ( (sbin = sieve_open(sieve_instance, filename, NULL, ehandler, NULL)) == NULL ) {
		sieve_error_handler_unref(&ehandler);
		i_fatal("Failed to compile sieve script\n");
	}

	sieve_error_handler_unref(&ehandler);
		
	return sbin;
}
	
void sieve_tool_dump_binary_to(struct sieve_binary *sbin, const char *filename)	
{
	int dfd = -1;
	struct ostream *dumpstream;
	
	if ( filename == NULL ) return;
	
	if ( strcmp(filename, "-") == 0 ) 
		dumpstream = o_stream_create_fd(1, 0, FALSE);
	else {
		if ( (dfd = open(filename, O_WRONLY | O_TRUNC | O_CREAT, 0600)) < 0 ) {
			i_fatal("failed to open dump-file for writing: %m");
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

