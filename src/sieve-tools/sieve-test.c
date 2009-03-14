/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "ostream.h"
#include "array.h"
#include "mail-namespace.h"
#include "mail-storage.h"
#include "env-util.h"

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
#ifdef SIEVE_RUNTIME_TRACE
#  define SVTRACE " [-t]"
#else
#  define SVTRACE
#endif
	printf(
"Usage: sieve-test [-r <recipient address>] [-f <envelope sender>]\n"
"                  [-m <mailbox>] [-d <dump filename>] [-x <extensions>]\n"
"                  [-s <scriptfile>] [-c]"SVTRACE"\n"
"                  <scriptfile> <mailfile>\n"
	);
}

/*
 * Dummy SMTP session
 */

static void *sieve_smtp_open(const char *destination,
	const char *return_path, FILE **file_r)
{
	i_info("sending message from <%s> to <%s>:",
		return_path == NULL || *return_path == '\0' ? "" : return_path, 
		destination);
	printf("\nSTART MESSAGE:\n");
	
	*file_r = stdout;
	
	return NULL;	
}

static bool sieve_smtp_close(void *handle ATTR_UNUSED)
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
	ARRAY_DEFINE(scriptfiles, const char *);
	const char *scriptfile, *recipient, *sender, *mailbox, *dumpfile, *mailfile, 
		*mailloc, *extensions; 
	const char *user, *home;
	int i;
	struct mail_raw *mailr;
	struct mail_namespace *ns = NULL;
	struct mail_user *mail_user = NULL;
	struct sieve_binary *main_sbin, *sbin = NULL;
	struct sieve_message_data msgdata;
	struct sieve_script_env scriptenv;
	struct sieve_exec_status estatus;
	struct sieve_error_handler *ehandler;
	struct ostream *teststream = NULL;
	bool force_compile = FALSE, execute = FALSE;
	bool trace = FALSE;
	int ret;

	sieve_tool_init();
	
	t_array_init(&scriptfiles, 16);

	/* Parse arguments */
	scriptfile = recipient = sender = mailbox = dumpfile = mailfile = mailloc = 
		extensions = NULL;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-r") == 0) {
			/* recipient address */
			i++;
			if (i == argc)
				i_fatal("Missing -r argument");
			recipient = argv[i];
		} else if (strcmp(argv[i], "-f") == 0) {
			/* envelope sender */
			i++;
			if (i == argc)
				i_fatal("Missing -f argument");
			sender = argv[i];
		} else if (strcmp(argv[i], "-m") == 0) {
			/* default mailbox (keep box) */
			i++;
			if (i == argc) 
				i_fatal("Missing -m argument");
			mailbox = argv[i];
		} else if (strcmp(argv[i], "-d") == 0) {
			/* dump file */
			i++;
			if (i == argc)
				i_fatal("Missing -d argument");
			dumpfile = argv[i];
		} else if (strcmp(argv[i], "-l") == 0) {
			/* mail location */
			i++;
			if (i == argc)
				i_fatal("Missing -l argument");
			mailloc = argv[i];
		} else if (strcmp(argv[i], "-x") == 0) {
			/* extensions */
			i++;
			if (i == argc)
				i_fatal("Missing -x argument");
			extensions = argv[i];
		} else if (strcmp(argv[i], "-s") == 0) {
			const char *file;
			
			/* scriptfile executed before main script */
			i++;
			if (i == argc)
				i_fatal("Missing -s argument");
				
			file = t_strdup(argv[i]);
			array_append(&scriptfiles, &file, 1);
		} else if (strcmp(argv[i], "-c") == 0) {
			/* force compile */
			force_compile = TRUE;
		} else if (strcmp(argv[i], "-e") == 0) {
			/* execute */
			execute = TRUE;
#ifdef SIEVE_RUNTIME_TRACE
		} else if (strcmp(argv[i], "-t") == 0) {
			/* runtime trace */
			trace = TRUE;
#endif
		} else if ( scriptfile == NULL ) {
			scriptfile = argv[i];
		} else if ( mailfile == NULL ) {
			mailfile = argv[i];
		} else {
			print_help();
			i_fatal("Unknown argument: %s", argv[i]);
		}
	}
	
	if ( scriptfile == NULL ) {
		print_help();
		i_fatal("Missing <scriptfile> argument");
	}
	
	if ( mailfile == NULL ) {
		print_help();
		i_fatal("Missing <mailfile> argument");
	}

	if ( extensions != NULL ) {
		sieve_set_extensions(extensions);
	}

	/* Register tool-specific extensions */
	(void) sieve_extension_register(&debug_extension, TRUE);
	
	/* Compile main sieve script */
	if ( force_compile ) {
		main_sbin = sieve_tool_script_compile(scriptfile, NULL);
		(void) sieve_save(main_sbin, NULL);
	} else {
		main_sbin = sieve_tool_script_open(scriptfile);
	}

	if ( main_sbin != NULL ) {
		/* Dump script */
		sieve_tool_dump_binary_to(main_sbin, dumpfile);
	
		user = sieve_tool_get_user();
		home = getenv("HOME");

		/* Initialize mail storages */
		mail_users_init(getenv("AUTH_SOCKET_PATH"), getenv("DEBUG") != NULL);
		mail_storage_init();
		mail_storage_register_all();
		mailbox_list_register_all();
	
		/* Obtain mail namespaces from -l argument */
		if ( mailloc != NULL ) {
			env_put(t_strdup_printf("NAMESPACE_1=%s", mailloc));
			env_put("NAMESPACE_1_INBOX=1");
			env_put("NAMESPACE_1_LIST=1");
			env_put("NAMESPACE_1_SEP=.");
			env_put("NAMESPACE_1_SUBSCRIPTIONS=1");

			mail_user = mail_user_init(user);
			mail_user_set_home(mail_user, home);
			if (mail_namespaces_init(mail_user) < 0)
				i_fatal("Namespace initialization failed");	

			ns = mail_user->namespaces;
		}

		/* Initialize raw mail object */
		mail_raw_init(user);
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
	
		/* Create error handler */
		ehandler = sieve_stderr_ehandler_create(0);	
		sieve_error_handler_accept_infolog(ehandler, TRUE);

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
					(&msgdata, &scriptenv, ehandler);
			else
				mscript = sieve_multiscript_start_test
					(&msgdata, &scriptenv, ehandler, teststream);
		
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
				more = sieve_multiscript_run(mscript, sbin, FALSE);
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
			
				sieve_multiscript_run(mscript, sbin, TRUE);
			}
			
			result = sieve_multiscript_finish(&mscript);
			
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
		
		/* Cleanup error handler */
		sieve_error_handler_unref(&ehandler);

		/* De-initialize raw mail object */
		mail_raw_close(mailr);
		mail_raw_deinit();

		/* De-initialize mail user object */
		if ( mail_user != NULL )
			mail_user_unref(&mail_user);

		/* De-initialize mail storages */
		mail_storage_deinit();
		mail_users_deinit();
	}

	sieve_tool_deinit();
	
	return 0;
}
