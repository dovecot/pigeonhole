/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve.h"
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
"Usage: sievec [-d] [-x <extensions>] <scriptfile> <outfile>\n"
	);
}

/* 
 * Tool implementation
 */

int main(int argc, char **argv) {
	int i;
	struct sieve_binary *sbin;
	bool dump = FALSE;
	const char *scriptfile, *outfile, *extensions;
		
	scriptfile = outfile = extensions = NULL;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-d") == 0) {
			/* dump file */
			dump = TRUE;
		} else if (strcmp(argv[i], "-x") == 0) {
			/* extensions */
			i++;
			if (i == argc)
				i_fatal("Missing -x argument");
			extensions = argv[i];
		} else if ( scriptfile == NULL ) {
			scriptfile = argv[i];
		} else if ( outfile == NULL ) {
			outfile = argv[i];
		} else {
			print_help();
			i_fatal("Unknown argument: %s", argv[i]);
		}
	}
	
	if ( scriptfile == NULL ) {
		print_help();
		i_fatal("Missing <scriptfile> argument");
	}
	
	if ( outfile == NULL ) {
		if ( !dump ) {
			print_help();
			i_fatal("The <outfile> argument is mandatory without -d");
		}
		outfile = "-";
	}

	sieve_tool_init();

	if ( extensions != NULL ) {
		sieve_set_extensions(extensions);
	}

	sbin = sieve_tool_script_compile(scriptfile);

	if ( sbin != NULL ) {
		if ( dump ) 
			sieve_tool_dump_binary_to(sbin, outfile);
		else {
			sieve_save(sbin, outfile);
		}
		
		sieve_close(&sbin);
	}
		
	sieve_tool_deinit();
}
