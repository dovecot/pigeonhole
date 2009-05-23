/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve.h"
#include "sieve-extensions.h"
#include "sieve-binary.h"
#include "sieve-tool.h"

#include "sieve-ext-debug.h"

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
	const char *binfile, *outfile, *extensions;
	
	sieve_tool_init(TRUE);
	
	binfile = outfile = extensions = NULL;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-x") == 0) {
			/* extensions */
			i++;
			if (i == argc)
				i_fatal("Missing -x argument");
			extensions = argv[i];
		} else if ( binfile == NULL ) {
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

	if ( extensions != NULL ) {
		sieve_set_extensions(extensions);
	}

	/* Register tool-specific extensions */
	(void) sieve_extension_register(&debug_extension, TRUE);
		
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

