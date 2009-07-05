/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "lib-signals.h"
#include "ioloop.h"
#include "env-util.h"
#include "str.h"
#include "ostream.h"
#include "array.h"
#include "mail-namespace.h"
#include "mail-storage.h"
#include "master-service.h"
#include "master-service-settings.h"
#include "mail-storage-service.h"

#include "sieve.h"
#include "sieve-binary.h"
#include "sieve-extensions.h"

#include "mail-raw.h"
#include "sieve-tool.h"

#include "sieve-ext-debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <sysexits.h>


/* 
 * Configuration
 */

#define DEFAULT_SENDMAIL_PATH "/usr/lib/sendmail"
#define DEFAULT_ENVELOPE_SENDER "MAILER-DAEMON"

/*
 * Print help
 */

static void print_help(void)
{
	printf(
"Usage: sieve-test [-c] [-d <dump-filename>] [-e] [-f <envelope-sender>]\n"
"                  [-l <mail-location>] [-m <default-mailbox>]\n" 
"                  [-r <recipient-address>] [-s <script-file>]\n"
"                  [-t] [-x <extensions>] <script-file> <mail-file>\n"
	);
}

/*
 * Dummy SMTP session
 */

static void *sieve_smtp_open
(void *script_ctx ATTR_UNUSED, const char *destination,
	const char *return_path, FILE **file_r)
{
	i_info("sending message from <%s> to <%s>:",
		return_path == NULL || *return_path == '\0' ? "" : return_path, 
		destination);
	printf("\nSTART MESSAGE:\n");
	
	*file_r = stdout;
	
	return NULL;	
}

static bool sieve_smtp_close
(void *script_ctx ATTR_UNUSED, void *handle ATTR_UNUSED)
{
	printf("END MESSAGE\n\n");
	return TRUE;
}

/*
 * Dummy duplicate check implementation
 */

static int duplicate_check(const void *id ATTR_UNUSED, size_t id_size ATTR_UNUSED, 
	const char *user)
{
	i_info("checked duplicate for user %s.\n", user);
	return 0;
}

static void duplicate_mark
(const void *id ATTR_UNUSED, size_t id_size ATTR_UNUSED, const char *user, 
	time_t time ATTR_UNUSED)
{
	i_info("marked duplicate for user %s.\n", user);
}

/*
 * Tool implementation
 */

int main(int argc, char **argv) 
{
	enum mail_storage_service_flags service_flags = 0;
    struct master_service *service;
    const char *getopt_str;
    int c;
	ARRAY_DEFINE(scriptfiles, const char *);
	const char *scriptfile, *recipient, *sender, *mailbox, *dumpfile, *mailfile, 
		*mailloc, *extensions; 
	const char *user, *home;
	struct mail_raw *mailr;
	struct mail_namespace_settings ns_set;
	struct mail_namespace *ns = NULL;
	struct sieve_binary *main_sbin, *sbin = NULL;
	struct sieve_message_data msgdata;
	struct sieve_script_env scriptenv;
	struct sieve_exec_status estatus;
	struct sieve_error_handler *ehandler;
	struct ostream *teststream = NULL;
	bool force_compile = FALSE, execute = FALSE;
	bool trace = FALSE;
	int ret;

	service = master_service_init("sieve-test",
                      MASTER_SERVICE_FLAG_STANDALONE,
                      argc, argv);

	sieve_tool_init(FALSE);

	t_array_init(&scriptfiles, 16);

    user = getenv("USER");
	
	/* Parse arguments */
	scriptfile = recipient = sender = mailbox = dumpfile = mailfile = mailloc = 
		extensions = NULL;
	getopt_str = t_strconcat("r:f:m:d:l:x:s:ect",
                 master_service_getopt_string(), NULL);
	while ((c = getopt(argc, argv, getopt_str)) > 0) {
		switch (c) {
		case 'r':
			/* destination address */
			recipient = optarg;
			break;
		case 'f':
			/* envelope sender address */
			sender = optarg;
			break;
		case 'm':
			/* default mailbox (keep box) */
			mailbox = optarg;
			break;
		case 'd':
			/* dump file */
			dumpfile = optarg;
			break;
        case 'l':
			/* mail location */
			mailloc = optarg;
			break;
        case 'x':
			/* mail location */
			extensions = optarg;
			break;
        case 's': 
			/* scriptfile executed before main script */
			{
				const char *file;			

				file = t_strdup(optarg);
				array_append(&scriptfiles, &file, 1);
			}
            break;

        case 'e':
            execute = TRUE;
            break;
        case 'c':
            force_compile = TRUE;
            break;
        case 't':
            trace = TRUE;
            break;
        default:
            if (!master_service_parse_option(service, c, optarg)) {
                print_help();
                i_fatal_status(EX_USAGE,
                           "Unknown argument: %c", c);
            }
            break;
        }
    }

	if ( optind < argc ) {
		scriptfile = t_strdup(argv[optind++]);
	} else { 
		print_help();
		i_fatal_status(EX_USAGE, "Missing <script-file> argument");
	}
	
	if ( optind < argc ) {
		mailfile = t_strdup(argv[optind++]);
	} else { 
		print_help();
		i_fatal_status(EX_USAGE, "Missing <mail-file> argument");
	}
	
	if (optind != argc) {
		print_help();
		i_fatal_status(EX_USAGE, "Unknown argument: %s", argv[optind]);
	}

	if ( extensions != NULL ) {
		sieve_set_extensions(extensions);
	}

	/* Register tool-specific extensions */
	(void) sieve_extension_register(&debug_extension, TRUE);
	
	/* Create error handler */
	ehandler = sieve_stderr_ehandler_create(0);
	sieve_system_ehandler_set(ehandler);
	sieve_error_handler_accept_infolog(ehandler, TRUE);

	/* Compile main sieve script */
	if ( force_compile ) {
		main_sbin = sieve_tool_script_compile(scriptfile, NULL);
		(void) sieve_save(main_sbin, NULL);
	} else {
		main_sbin = sieve_tool_script_open(scriptfile);
	}

	if ( main_sbin != NULL ) {
		struct mail_user *mail_user_dovecot = NULL;
		struct mail_user *mail_user = NULL;
		struct mail_storage_service_input input;

		/* Dump script */
		sieve_tool_dump_binary_to(main_sbin, dumpfile);
	
		user = sieve_tool_get_user();
		home = getenv("HOME");

		/* Initialize mail storages */
		//env_remove("HOME");
		env_put("DOVECONF_ENV=1");
		env_put(t_strdup_printf("MAIL=maildir:/tmp/dovecot-test-%s", user));

		memset(&input, 0, sizeof(input));
		input.username = user;
		mail_user_dovecot = mail_storage_service_init_user(service, &input,
                NULL, service_flags);
	
		/* Obtain mail namespaces from -l argument */
		if ( mailloc != NULL ) {
			const char *errstr;

			mail_user = mail_user_alloc(user, mail_user_dovecot->unexpanded_set);
			mail_user_set_home(mail_user, home);

			if (mail_user_init(mail_user, &errstr) < 0)
        		i_fatal("Test user initialization failed: %s", errstr);

			memset(&ns_set, 0, sizeof(ns_set));
    		ns_set.location = mailloc;

    		ns = mail_namespaces_init_empty(mail_user);
    		ns->flags |= NAMESPACE_FLAG_NOQUOTA | NAMESPACE_FLAG_NOACL;
    		ns->set = &ns_set;
		}

		if (master_service_set(service, "mail_full_filesystem_access=yes") < 0)
			i_unreached(); 

		/* Initialize raw mail object */
		mail_raw_init(service, user, mail_user_dovecot);
		mailr = mail_raw_open_file(mailfile);

		sieve_tool_get_envelope_data(mailr->mail, &recipient, &sender);

		if ( mailbox == NULL )
			mailbox = "INBOX";

		/* Collect necessary message data */
		memset(&msgdata, 0, sizeof(msgdata));
		msgdata.mail = mailr->mail;
		msgdata.return_path = sender;
		msgdata.to_address = recipient;
		msgdata.auth_user = user;
		(void)mail_get_first_header(mailr->mail, "Message-ID", &msgdata.id);

		/* Create stream for test and trace output */
		if ( !execute || trace )
			teststream = o_stream_create_fd(1, 0, FALSE);	
		
		/* Compose script environment */
		memset(&scriptenv, 0, sizeof(scriptenv));
		scriptenv.default_mailbox = "INBOX";
		scriptenv.namespaces = ns;
		scriptenv.username = user;
		scriptenv.hostname = "host.example.com";
		scriptenv.postmaster_address = "postmaster@example.com";
		scriptenv.smtp_open = sieve_smtp_open;
		scriptenv.smtp_close = sieve_smtp_close;
		scriptenv.duplicate_mark = duplicate_mark;
		scriptenv.duplicate_check = duplicate_check;
		scriptenv.trace_stream = ( trace ? teststream : NULL );
		scriptenv.exec_status = &estatus;
	
		/* Run the test */
		ret = 1;
		if ( array_count(&scriptfiles) == 0 ) {
			/* Single script */
			sbin = main_sbin;
			main_sbin = NULL;
	
			/* Execute/Test script */
			if ( execute )
				ret = sieve_execute(sbin, &msgdata, &scriptenv, ehandler);
			else
				ret = sieve_test(sbin, &msgdata, &scriptenv, ehandler, teststream);				
		} else {
			/* Multiple scripts */
			const char *const *sfiles;
			unsigned int i, count;
			struct sieve_multiscript *mscript;
			bool more = TRUE;
			int result;

			if ( execute )
				mscript = sieve_multiscript_start_execute
					(&msgdata, &scriptenv);
			else
				mscript = sieve_multiscript_start_test
					(&msgdata, &scriptenv, teststream);
		
			/* Execute scripts sequentially */
			sfiles = array_get(&scriptfiles, &count); 
			for ( i = 0; i < count && more; i++ ) {
				if ( teststream != NULL ) 
					o_stream_send_str(teststream, 
						t_strdup_printf("\n## Executing script: %s\n", sfiles[i]));
			
				/* Close previous script */
				if ( sbin != NULL )						
					sieve_close(&sbin);
		
				/* Compile sieve script */
				if ( force_compile ) {
					sbin = sieve_tool_script_compile(sfiles[i], sfiles[i]);
					(void) sieve_save(sbin, NULL);
				} else {
					sbin = sieve_tool_script_open(sfiles[i]);
				}
			
				if ( sbin == NULL ) {
					ret = SIEVE_EXEC_FAILURE;
					break;
				}
			
				/* Execute/Test script */
				more = sieve_multiscript_run(mscript, sbin, ehandler, FALSE);
			}
		
			/* Execute/Test main script */
			if ( more && ret > 0 ) {
				if ( teststream != NULL ) 
					o_stream_send_str(teststream, 
						t_strdup_printf("## Executing script: %s\n", scriptfile));
				
				/* Close previous script */
				if ( sbin != NULL )						
					sieve_close(&sbin);	
				
				sbin = main_sbin;
				main_sbin = NULL;
			
				sieve_multiscript_run(mscript, sbin, ehandler, TRUE);
			}
			
			result = sieve_multiscript_finish(&mscript, ehandler);
			
			ret = ret > 0 ? result : ret;
		}
	
		/* Run */
		switch ( ret ) {
		case SIEVE_EXEC_OK:
			i_info("final result: success");
			break;
		case SIEVE_EXEC_BIN_CORRUPT:
			i_info("corrupt binary deleted.");
			(void) unlink(sieve_binary_path(sbin));
		case SIEVE_EXEC_FAILURE:
			i_info("final result: failed; resolved with successful implicit keep");
			break;
		case SIEVE_EXEC_KEEP_FAILED:
			i_info("final result: utter failure");
			break;
		default:
			i_info("final result: unrecognized return value?!");	
		}

		if ( teststream != NULL )
			o_stream_destroy(&teststream);

		/* Cleanup remaining binaries */
		sieve_close(&sbin);
		if ( main_sbin != NULL ) sieve_close(&main_sbin);
		
		/* De-initialize raw mail object */
		mail_raw_close(mailr);
		mail_raw_deinit();

		/* De-initialize mail user objects */
		if ( mail_user != NULL )
			mail_user_unref(&mail_user);

		if ( mail_user_dovecot != NULL )
			mail_user_unref(&mail_user_dovecot);
	
		mail_storage_service_deinit_user();
	}

	/* Cleanup error handler */
	sieve_error_handler_unref(&ehandler);
	sieve_system_ehandler_reset();

	sieve_tool_deinit();

    master_service_deinit(&service);
	
	return 0;
}
