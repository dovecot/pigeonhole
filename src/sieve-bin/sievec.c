/* Copyright (c) 2002-2007 Dovecot authors, see the included COPYING file */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "lib.h"
#include "sieve.h"

#include "bin-common.h"

static void print_help(void)
{
	printf(
"Usage: sievec [-d] <scriptfile> <outfile>\n"
	);
}

int main(int argc, char **argv) {
	int i;
	struct sieve_binary *sbin;
	bool dump = FALSE;
	const char *scriptfile, *outfile;
	
	bin_init();
	
	scriptfile = outfile = NULL;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-d") == 0) {
			/* dump file */
			dump = TRUE;
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
		print_help();
		i_fatal("Missing <outfile> argument");
	}

	sbin = bin_compile_sieve_script(scriptfile);

	if ( sbin != NULL ) {
		if ( dump ) 
			bin_dump_sieve_binary_to(sbin, "-");
		else {
			sieve_binary_save(sbin, outfile);
		}
		
		sieve_close(&sbin);
	}
		
	bin_deinit();
}
