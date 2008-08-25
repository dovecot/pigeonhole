/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "lib.h"
#include "sieve.h"
#include "sieve-binary.h"

#include "bin-common.h"

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
			i_fatal("Unknown argument: %s", argv[i]);
		}
	}
	
	if ( binfile == NULL ) {
		print_help();
		i_fatal("Missing <binfile> argument");
	}
	
	bin_init();
	
	sbin = sieve_binary_open(binfile, NULL);

	if ( sbin != NULL && !sieve_binary_load(sbin) ) {
		sieve_binary_unref(&sbin);
		sbin = NULL;
	}

	if ( sbin != NULL ) {
		bin_dump_sieve_binary_to(sbin, outfile == NULL ? "-" : outfile);
	
		sieve_binary_unref(&sbin);
	} else 
		i_error("Failed to load binary: %s", binfile);
	
	bin_deinit();
}

