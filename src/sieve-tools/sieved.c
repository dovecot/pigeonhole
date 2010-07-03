/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "master-service.h"
#include "master-service-settings.h"
#include "mail-storage-service.h"
#include "mail-user.h"

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
	enum master_service_flags service_flags =
		MASTER_SERVICE_FLAG_STANDALONE;
	enum mail_storage_service_flags storage_service_flags =
		MAIL_STORAGE_SERVICE_FLAG_NO_CHDIR |
		MAIL_STORAGE_SERVICE_FLAG_NO_LOG_INIT;
	struct mail_storage_service_ctx *storage_service;
	struct mail_storage_service_user *service_user;
	struct mail_storage_service_input service_input;
	struct mail_user *mail_user_dovecot = NULL;
	ARRAY_TYPE(const_string) plugins;
	struct sieve_binary *sbin;
	const char *binfile, *outfile, *extensions, *username;
	const char *errstr;
	int exit_status = EXIT_SUCCESS;
	int c;

	master_service = master_service_init("sieved", 
		service_flags, &argc, &argv, "x:P:");
	
	t_array_init(&plugins, 4);
	
	binfile = outfile = extensions = NULL;
	username = getenv("USER");
	while ((c = master_getopt(master_service)) > 0) {
		switch (c) {
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
		binfile = argv[optind++];
	} else { 
		print_help();
		i_fatal_status(EX_USAGE, "Missing <script-file> argument");
	}

	if ( optind < argc ) {
		outfile = argv[optind++];
	} 
	
	/* Initialize Service */

	master_service_init_finish(master_service);

	memset(&service_input, 0, sizeof(service_input));
	service_input.module = "sieved";
	service_input.service = "sieved";
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
		
	sbin = sieve_load(sieve_instance, binfile);

	if ( sbin != NULL ) {
		sieve_tool_dump_binary_to(sbin, outfile == NULL ? "-" : outfile);
	
		sieve_close(&sbin);
	} else {
		i_error("failed to load binary: %s", binfile);
		exit_status = EXIT_FAILURE;
	}

	sieve_tool_deinit();

	if ( mail_user_dovecot != NULL )
		mail_user_unref(&mail_user_dovecot);

	mail_storage_service_user_free(&service_user);
	mail_storage_service_deinit(&storage_service);
	master_service_deinit(&master_service);

	return exit_status;
}

