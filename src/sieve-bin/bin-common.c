/* Copyright (c) 2005-2007 Dovecot authors, see the included COPYING file */

#include "lib.h"
#include "lib-signals.h"
#include "ioloop.h"
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
	io_loop_stop(ioloop);
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
}

void bin_deinit(void)
{
	lib_signals_deinit();

	io_loop_destroy(&ioloop);
	lib_deinit();
}
	
