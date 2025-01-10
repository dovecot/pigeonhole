/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "master-service.h"
#include "master-service-settings.h"
#include "mail-storage-service.h"
#include "mail-user.h"

#include "sieve.h"
#include "sieve-extensions.h"
#include "sieve-script.h"
#include "sieve-tool.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <dirent.h>
#include <sysexits.h>

/*
 * Print help
 */

static void print_help(void)
{
	printf(
"Usage: sievec  [-c <config-file>] [-d] [-D] [-P <plugin>] [-x <extensions>] \n"
"              <script-file> [<out-file>]\n"
	);
}

/*
 * Tool implementation
 */

int main(int argc, char **argv)
{
	struct sieve_instance *svinst;
	struct stat st;
	struct sieve_binary *sbin;
	bool dump = FALSE;
	const char *scriptfile, *outfile;
	int exit_status = EXIT_SUCCESS;
	int c;

	sieve_tool = sieve_tool_init("sievec", &argc, &argv, "DdP:x:u:", FALSE);

	outfile = NULL;
	while ((c = sieve_tool_getopt(sieve_tool)) > 0) {
		switch (c) {
		case 'd':
			/* dump file */
			dump = TRUE;
			break;
		default:
			print_help();
			i_fatal_status(EX_USAGE, "Unknown argument: %c", c);
			break;
		}
	}

	if (optind < argc) {
		scriptfile = argv[optind++];
	} else {
		print_help();
		i_fatal_status(EX_USAGE, "Missing <script-file> argument");
	}

	if (optind < argc) {
		outfile = argv[optind++];
	} else if (dump) {
		outfile = "-";
	}

	svinst = sieve_tool_init_finish(sieve_tool, FALSE, TRUE);

	/* Enable debug extension */
	sieve_enable_debug_extension(svinst);

	if (stat(scriptfile, &st) == 0 && S_ISDIR(st.st_mode)) {
		/* Script directory */
		DIR *dirp;
		struct dirent *dp;

		/* Sanity checks on some of the arguments */

		if (dump)
			i_fatal_status(EX_USAGE,
				"the -d option is not allowed when scriptfile is a directory.");

		if (outfile != NULL)
			i_fatal_status(EX_USAGE,
				"the outfile argument is not allowed when scriptfile is a directory.");

		/* Open the directory */
		dirp = opendir(scriptfile);
		if (dirp == NULL)
			i_fatal("opendir(%s) failed: %m", scriptfile);

		/* Compile each sieve file */
		for (;;) {
			errno = 0;
			dp = readdir(dirp);
			if (dp == NULL) {
				if (errno != 0) {
					i_fatal("readdir(%s) failed: %m",
						scriptfile);
				}
				break;
			}

			if (sieve_script_file_has_extension(dp->d_name)) {
				const char *file;

				if (scriptfile[strlen(scriptfile)-1] == '/')
					file = t_strconcat(scriptfile, dp->d_name, NULL);
				else
					file = t_strconcat(scriptfile, "/", dp->d_name, NULL);

				sbin = sieve_tool_script_compile(sieve_tool, file);

				if (sbin != NULL) {
					sieve_save(sbin, TRUE, NULL);
					sieve_close(&sbin);
				}
			}
		}

		/* Close the directory */
		if (closedir(dirp) < 0)
			i_fatal("closedir(%s) failed: %m", scriptfile);
	} else {
		/* Script file (i.e. not a directory)

		   NOTE: For consistency, stat errors are handled here as well
		 */
		sbin = sieve_tool_script_compile(sieve_tool, scriptfile);
		if (sbin != NULL) {
			if (dump)
				sieve_tool_dump_binary_to(sbin, outfile, FALSE);
			else
				sieve_save_as(sbin, outfile, TRUE, 0600, NULL);

			sieve_close(&sbin);
		} else {
			exit_status = EXIT_FAILURE;
		}
	}

	sieve_tool_deinit(&sieve_tool);

	return exit_status;
}
