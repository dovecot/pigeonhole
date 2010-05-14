/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"

#include "sieve.h"
#include "sieve-extensions.h"
#include "sieve-tool.h"

#include "sieve-ext-debug.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sysexits.h>

/*
 * Print help
 */

static void print_help(void)
{
	printf(
"Usage: sieved [-P <plugin>] [-x <extensions>]\n"
"              <sieve-binary> [<out-file>]\n"
	);
}

/*
 * Tool implementation
 */

int main(int argc, char **argv) 
{
	ARRAY_TYPE(const_string) plugins;
	int i;
	struct sieve_binary *sbin;
	const char *binfile, *outfile, *extensions;
	int exit_status = EXIT_SUCCESS;
	
	sieve_tool_init(TRUE);

	t_array_init(&plugins, 4);
	
	binfile = outfile = extensions = NULL;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-x") == 0) {
			/* extensions */
			i++;
			if (i == argc) {
				print_help();
				i_fatal_status(EX_USAGE, "Missing -x argument");
			}
			extensions = argv[i];
		} else if (strcmp(argv[i], "-P") == 0) {
			const char *plugin;

			/* scriptfile executed before main script */
			i++;
			if (i == argc) {
				print_help();
				i_fatal_status(EX_USAGE, "Missing -P argument");
			}

			plugin = t_strdup(argv[i]);
			array_append(&plugins, &plugin, 1);
		} else if ( binfile == NULL ) {
			binfile = argv[i];
		} else if ( outfile == NULL ) {
			outfile = argv[i];
		} else {
			print_help();
			i_fatal_status(EX_USAGE, "unknown argument: %s", argv[i]);
		}
	}
	
	if ( binfile == NULL ) {
		print_help();
		i_fatal_status(EX_USAGE, "missing <sieve-binary> argument");
	}

	sieve_tool_sieve_init(NULL, FALSE);

	if ( array_count(&plugins) > 0 ) {
		sieve_tool_load_plugins(&plugins);
	}

	if ( extensions != NULL ) {
		sieve_set_extensions(sieve_instance, extensions);
	}

	/* Register tool-specific extensions */
	(void) sieve_extension_register(sieve_instance, &debug_extension, TRUE);
		
	sbin = sieve_load(sieve_instance, binfile);

	if ( sbin != NULL ) {
		sieve_tool_dump_binary_to(sbin, outfile == NULL ? "-" : outfile);
	
		sieve_close(&sbin);
	} else {
		i_error("failed to load binary: %s", binfile);
		exit_status = EXIT_FAILURE;
	}

	sieve_tool_deinit();

	return exit_status;
}

