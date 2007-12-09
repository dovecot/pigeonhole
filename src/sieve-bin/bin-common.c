#include "lib.h"
#include "lib-signals.h"
#include "ioloop.h"
#include "ostream.h"
#include "hostpid.h"

#include "sieve.h"
#include "bin-common.h"

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

void bin_init(void) 
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
}

void bin_deinit(void)
{
	sieve_deinit();
	
	lib_signals_deinit();

	io_loop_destroy(&ioloop);
	lib_deinit();
}

const char *bin_get_user(void)
{
	uid_t process_euid = geteuid();
	struct passwd *pw = getpwuid(process_euid);
	if (pw != NULL) {
		return t_strdup(pw->pw_name);
	} 
		
	i_fatal("Couldn't lookup our username (uid=%s)", dec2str(process_euid));
	return NULL;
}

struct sieve_binary *bin_compile_sieve_script(const char *filename)
{
	struct sieve_error_handler *ehandler;
	struct sieve_binary *sbin;
	
	ehandler = sieve_stderr_ehandler_create();
	sieve_error_handler_accept_infolog(ehandler, TRUE);

	if ( (sbin = sieve_compile(filename, ehandler)) == NULL ) {
		sieve_error_handler_free(&ehandler);
		i_fatal("Failed to compile sieve script\n");
	}

	sieve_error_handler_free(&ehandler);
		
	return sbin;
}
	
struct sieve_binary *bin_open_sieve_script(const char *filename)
{
	struct sieve_error_handler *ehandler;
	struct sieve_binary *sbin;
	
	ehandler = sieve_stderr_ehandler_create();
	sieve_error_handler_accept_infolog(ehandler, TRUE);

	if ( (sbin = sieve_open(filename, ehandler)) == NULL ) {
		sieve_error_handler_free(&ehandler);
		i_fatal("Failed to compile sieve script\n");
	}

	sieve_error_handler_free(&ehandler);
		
	return sbin;
}
	
void bin_dump_sieve_binary_to(struct sieve_binary *sbin, const char *filename)	
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

int bin_open_mail_file(const char *filename)
{
	int mfd;
	
	if ( strcmp(filename, "-") == 0 )
		return 0;

	if ( (mfd = open(filename, O_RDONLY)) < 0 ) 
		i_fatal("Failed to open mail file: %m");			
	
	return mfd;
}

void bin_close_mail_file(int mfd)
{
	if ( mfd != 0 )
		close(mfd);
}

void bin_fill_in_envelope
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


