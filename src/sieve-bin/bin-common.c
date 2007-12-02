/* Copyright (c) 2005-2007 Dovecot authors, see the included COPYING file */

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
		i_fatal("failed to initialize sieve implementation\n");
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
		
	i_fatal("couldn't lookup our username (uid=%s)", dec2str(process_euid));
	return NULL;
}

struct sieve_binary *bin_compile_sieve_script(const char *filename)
{
	int sfd;
	struct sieve_binary *sbin;
	
	i_info("compiling sieve script '%s'...\n", filename);

	if ( (sfd = open(filename, O_RDONLY)) < 0 ) 
		i_info("failed to open sieve script %s: %m", filename);
	
	if ( (sbin = sieve_compile(sfd, FALSE)) == NULL ) 
	{
		close(sfd);
		i_fatal("failed to compile sieve script\n");
	}
		
	close(sfd);
	return sbin;
}
	
void bin_dump_sieve_binary_to(struct sieve_binary *sbin, const char *filename)	
{
	int dfd = -1;
	struct ostream *dumpstream;
	
	if ( strcmp(filename, "-") == 0 ) 
		dumpstream = o_stream_create_fd(1, 0, FALSE);
	else {
		if ( (dfd = open(filename, O_WRONLY)) < 0 ) {
			i_fatal("failed to open dump-file for writing: %m");
			exit(1);
		}
		
		dumpstream = o_stream_create_fd(dfd, 0, FALSE);
	}
	
	if ( dumpstream != NULL ) {
		(void) sieve_dump(sbin, dumpstream);
		o_stream_destroy(&dumpstream);
	} else {
		i_fatal("failed to create stream for sieve code dump.");
	}
	
	if ( dfd != -1 )
		close(dfd);
}	
