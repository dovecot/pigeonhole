/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve.h"
#include "sieve-binary.h"
#include "sieve-tool.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

/*
 * Print help
 */

static void print_help(void)
{
	printf(
"Usage: sieved <binfile> [<outfile>]\n"
	);
}

/*
 * Tool implementation
 */

int main(int argc, char **argv) {
	int i;
	struct sieve_binary *sbin;
	const char *binfile, *outfile;
		
	binfile = outfile = NULL;
	for (i = 1; i < argc; i++) {
		if ( binfile == NULL ) {
			binfile = argv[i];
		} else if ( outfile == NULL ) {
			outfile = argv[i];
		} else {
			print_help();
			i_fatal("unknown argument: %s", argv[i]);
		}
	}
	
	if ( binfile == NULL ) {
		print_help();
		i_fatal("missing <binfile> argument");
	}
	
	sieve_tool_init();
	
	sbin = sieve_binary_open(binfile, NULL);

	if ( sbin != NULL && !sieve_binary_load(sbin) ) {
		sieve_binary_unref(&sbin);
		sbin = NULL;
	}

	if ( sbin != NULL ) {
		sieve_tool_dump_binary_to(sbin, outfile == NULL ? "-" : outfile);
	
		sieve_binary_unref(&sbin);
	} else 
		i_error("failed to load binary: %s", binfile);
	
	sieve_tool_deinit();
}

