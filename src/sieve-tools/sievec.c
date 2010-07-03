/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "master-service.h"
#include "master-service-settings.h"
#include "mail-storage-service.h"

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
#include <sysexits.h>

/*
 * Print help
 */

static void print_help(void)
{
	printf(
"Usage: sievec [-d] [-P <plugin>] [-x <extensions>] \n"
"              <script-file> [<out-file>]\n"
	);
}

/* 
 * Tool implementation
 */

int main(int argc, char **argv) 
{
	enum master_service_flags service_flags =
		MASTER_SERVICE_FLAG_STANDALONE |
		MASTER_SERVICE_FLAG_KEEP_CONFIG_OPEN;
	enum mail_storage_service_flags storage_service_flags =
		MAIL_STORAGE_SERVICE_FLAG_NO_CHDIR |
		MAIL_STORAGE_SERVICE_FLAG_NO_LOG_INIT;
	struct mail_storage_service_ctx *storage_service;
	struct mail_storage_service_user *service_user;
	struct mail_storage_service_input service_input;
	struct mail_user *mail_user_dovecot = NULL;
	ARRAY_TYPE(const_string) plugins;
	struct stat st;
	struct sieve_binary *sbin;
	bool dump = FALSE;
	const char *scriptfile, *outfile, *extensions, *username;
	const char *errstr;
	int exit_status = EXIT_SUCCESS;
	int c;
		
	master_service = master_service_init("sievec", 
		service_flags, &argc, &argv, "dx:P:");

	t_array_init(&plugins, 4);	
		
	scriptfile = outfile = extensions = NULL;
	username = getenv("USER");
	while ((c = master_getopt(master_service)) > 0) {
		switch (c) {
		case 'd':
			/* dump file */
			dump = TRUE;
			break;
		case 'x':
			/* extensions */
			extensions = optarg;
			break;
		case 'P': 
			/* Plugin */
			{
				const char *plugin;			

				plugin = t_strdup(optarg);
				array_append(&plugins, &plugin, 1);
			}
			break;
		default:
			print_help();
			i_fatal_status(EX_USAGE, "Unknown argument: %c", c);
			break;
		}
	}

	if ( optind < argc ) {
		scriptfile = argv[optind++];
	} else { 
		print_help();
		i_fatal_status(EX_USAGE, "Missing <script-file> argument");
	}

	if ( optind < argc ) {
		outfile = argv[optind++];
	} else if ( dump ) {
		outfile = "-";
	}

	master_service_init_finish(master_service);

	memset(&service_input, 0, sizeof(service_input));
	service_input.module = "sievec";
	service_input.service = "sievec";
	service_input.username = username;

	storage_service = mail_storage_service_init
		(master_service, NULL, storage_service_flags);
	if (mail_storage_service_lookup_next(storage_service, &service_input,
					     &service_user, &mail_user_dovecot, &errstr) <= 0)
		i_fatal("%s", errstr);

	/* Initialize Sieve */

	sieve_tool_init(NULL, (void *) mail_user_dovecot, FALSE);

	if ( array_count(&plugins) > 0 ) {
		sieve_tool_load_plugins(&plugins);
	}

	if ( extensions != NULL ) {
		sieve_set_extensions(sieve_instance, extensions);
	}

	/* Register tool-specific extensions */
	(void) sieve_extension_register(sieve_instance, &debug_extension, TRUE);

	if ( stat(scriptfile, &st) == 0 && S_ISDIR(st.st_mode) ) {
		/* Script directory */
		DIR *dirp;
		struct dirent *dp;
		
		/* Sanity checks on some of the arguments */
		
		if ( dump )
			i_fatal_status(EX_USAGE, 
				"the -d option is not allowed when scriptfile is a directory."); 
		
		if ( outfile != NULL )
			i_fatal_status(EX_USAGE, 
				"the outfile argument is not allowed when scriptfile is a directory."); 
		
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
		} else {
			exit_status = EXIT_FAILURE;
		}
	}
		
	sieve_tool_deinit();

	if ( mail_user_dovecot != NULL )
		mail_user_unref(&mail_user_dovecot);

	mail_storage_service_user_free(&service_user);
	mail_storage_service_deinit(&storage_service);
	master_service_deinit(&master_service);

	return exit_status;
}
