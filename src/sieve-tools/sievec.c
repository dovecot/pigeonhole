/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve.h"
#include "sieve-extensions.h"
#include "sieve-script.h"
#include "sieve-tool.h"

#include "sieve-ext-debug.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <dirent.h>

/*
 * Print help
 */

static void print_help(void)
{
	printf(
"Usage: sievec [-d] [-x <extensions>] <scriptfile> [<outfile>]\n"
	);
}

/* 
 * Tool implementation
 */

int main(int argc, char **argv) {
	int i;
	struct stat st;
	struct sieve_binary *sbin;
	bool dump = FALSE;
	const char *scriptfile, *outfile, *extensions;
		
	sieve_tool_init();	
		
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
	
	if ( outfile == NULL && dump )
		outfile = "-";

	if ( extensions != NULL ) {
		sieve_set_extensions(extensions);
	}

	/* Register tool-specific extensions */
	(void) sieve_extension_register(&debug_extension, TRUE);

	if ( stat(scriptfile, &st) == 0 && S_ISDIR(st.st_mode) ) {
		/* Script directory */
		DIR *dirp;
		struct dirent *dp;
		
		/* Sanity checks on some of the arguments */
		
		if ( dump )
			i_fatal("the -d option is not allowed when scriptfile is a directory."); 
		
		if ( outfile != NULL )
			i_fatal("the outfile argument is not allowed when scriptfile is a "
				"directory."); 
		
		/* Open the directory */
		if ( (dirp = opendir(scriptfile)) == NULL )
			i_fatal("opendir(%s) failed: %m", scriptfile);
			
		/* Compile each sieve file */
		for (;;) {
		
			errno = 0;
			if ( (dp = readdir(dirp)) == NULL ) {
				if ( errno != 0 ) 
					i_fatal("readdir(%s) failed: %m", scriptfile);
				break;
			}
											
			if ( sieve_script_file_has_extension(dp->d_name) ) {
				const char *file;
				
				if ( scriptfile[strlen(scriptfile)-1] == '/' )
					file = t_strconcat(scriptfile, dp->d_name, NULL);
				else
					file = t_strconcat(scriptfile, "/", dp->d_name, NULL);

				sbin = sieve_tool_script_compile(file, dp->d_name);

				if ( sbin != NULL ) {
					sieve_save(sbin, NULL);
		
					sieve_close(&sbin);
				}
			}
		}
   
		/* Close the directory */
		if ( closedir(dirp) < 0 ) 
			i_fatal("closedir(%s) failed: %m", scriptfile);
 	
	} else {
		/* Script file (i.e. not a directory)
		 * 
		 *   NOTE: For consistency, stat errors are handled here as well 
		 */	
		sbin = sieve_tool_script_compile(scriptfile, NULL);

		if ( sbin != NULL ) {
			if ( dump ) 
				sieve_tool_dump_binary_to(sbin, outfile);
			else {
				sieve_save(sbin, outfile);
			}
		
			sieve_close(&sbin);
		}
	}
		
	sieve_tool_deinit();
}
