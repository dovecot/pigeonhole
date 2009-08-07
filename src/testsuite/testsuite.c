/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "lib-signals.h"
#include "ioloop.h"
#include "env-util.h"
#include "ostream.h"
#include "hostpid.h"
#include "mail-storage.h"
#include "mail-namespace.h"
#include "master-service.h"
#include "master-service-settings.h"
#include "mail-storage-service.h"

#include "sieve.h"
#include "sieve-extensions.h"
#include "sieve-script.h"
#include "sieve-binary.h"
#include "sieve-result.h"
#include "sieve-interpreter.h"

#include "mail-raw.h"
#include "sieve-tool.h"

#include "testsuite-common.h"
#include "testsuite-result.h"
#include "testsuite-message.h"
#include "testsuite-smtp.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <sysexits.h>

const struct sieve_script_env *testsuite_scriptenv;

/*
 * Configuration
 */

#define DEFAULT_SENDMAIL_PATH "/usr/lib/sendmail"
#define DEFAULT_ENVELOPE_SENDER "MAILER-DAEMON"

/*
 * Testsuite initialization 
 */

static void testsuite_tool_init(const char *extensions) 
{
	sieve_tool_init(FALSE);

	sieve_extensions_set_string(extensions);

	(void) sieve_extension_register(&testsuite_extension, TRUE);
	
	testsuite_init();
}

static void testsuite_tool_deinit(void)
{
	testsuite_deinit();
	
	sieve_tool_deinit();
}

/*
 * Testsuite execution
 */

static void print_help(void)
{
	printf(
"Usage: testsuite [-d <dump filename>] <scriptfile>\n"
	);
}

static int testsuite_run
(struct sieve_binary *sbin, const struct sieve_message_data *msgdata, 
	const struct sieve_script_env *senv, struct sieve_error_handler *ehandler)
{
	struct sieve_interpreter *interp;
	struct sieve_result *result;
	int ret = 0;

	/* Create the interpreter */
	if ( (interp=sieve_interpreter_create(sbin, ehandler)) == NULL )
		return SIEVE_EXEC_BIN_CORRUPT;

	/* Reset execution status */
	if ( senv->exec_status != NULL )
		memset(senv->exec_status, 0, sizeof(*senv->exec_status));

	/* Run the interpreter */
	result = testsuite_result_get();
	sieve_result_ref(result);
	ret = sieve_interpreter_run(interp, msgdata, senv, result);
	sieve_result_unref(&result);

	/* Free the interpreter */
	sieve_interpreter_free(&interp);

	return ret;
}

/* IEW.. YUCK.. and so forth.. */
static const char *_get_cwd(void)
{
	static char cwd[PATH_MAX];
	const char *result;

	result = t_strdup(getcwd(cwd, sizeof(cwd)));

	return result;
}

int main(int argc, char **argv) 
{
	enum mail_storage_service_flags service_flags = 0;
	struct master_service *service;
	const char *getopt_str;
	int c;
	const char *scriptfile, *dumpfile, *extensions; 
	const char *user;
	struct sieve_binary *sbin;
	const char *sieve_dir;
	bool trace = FALSE;
	int ret;

	service = master_service_init("testsuite",
                      MASTER_SERVICE_FLAG_STANDALONE,
                      argc, argv);

    user = getenv("USER");

	/* Parse arguments */
	scriptfile = dumpfile = extensions = NULL;

	getopt_str = t_strconcat("d:x:t",
                 master_service_getopt_string(), NULL);
	while ((c = getopt(argc, argv, getopt_str)) > 0) {
		switch (c) {
		case 'd':
			/* destination address */
			dumpfile = optarg;
			break;
		case 'x':
            /* destination address */
            extensions = optarg;
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
		i_fatal_status(EX_USAGE, "Missing <scriptfile> argument");
	}
	
	if (optind != argc) {
        print_help();
        i_fatal_status(EX_USAGE, "Unknown argument: %s", argv[optind]);
    }

	/* Initialize testsuite */
	testsuite_tool_init(extensions);

	printf("Test case: %s:\n\n", scriptfile);

	/* Initialize environment */
	sieve_dir = strrchr(scriptfile, '/');
	if ( sieve_dir == NULL )
		sieve_dir= "./";
	else
		sieve_dir = t_strdup_until(scriptfile, sieve_dir+1);

	/* Currently needed for include (FIXME) */
	env_put(t_strconcat("SIEVE_DIR=", sieve_dir, "included", NULL));
	env_put(t_strconcat("SIEVE_GLOBAL_DIR=", sieve_dir, "included-global", NULL));
	
	/* Compile sieve script */
	if ( (sbin = sieve_tool_script_compile(scriptfile, NULL)) != NULL ) {
		struct sieve_error_handler *ehandler;
	    struct mail_storage_service_input input;
		struct sieve_script_env scriptenv;
		struct mail_user *mail_user;

		/* Dump script */
		sieve_tool_dump_binary_to(sbin, dumpfile);
	
		/* Initialize mail user */
		user = sieve_tool_get_user();
		env_put("DOVECONF_ENV=1");
		env_put(t_strdup_printf("HOME=%s", _get_cwd()));
		env_put(t_strdup_printf("MAIL=maildir:/tmp/dovecot-test-%s", user));

	    memset(&input, 0, sizeof(input));
	    input.username = user;
		mail_user = mail_storage_service_init_user
			(service, &input, NULL, service_flags);

		if (master_service_set(service, "mail_full_filesystem_access=yes") < 0)
			i_unreached(); 

		testsuite_message_init(service, user, mail_user);

		memset(&scriptenv, 0, sizeof(scriptenv));
		scriptenv.default_mailbox = "INBOX";
		scriptenv.hostname = "testsuite.example.com";
		scriptenv.username = user;
		scriptenv.smtp_open = testsuite_smtp_open;
		scriptenv.smtp_close = testsuite_smtp_close;
		scriptenv.trace_stream = ( trace ? o_stream_create_fd(1, 0, FALSE) : NULL );

		testsuite_scriptenv = &scriptenv;

		testsuite_result_init();

		/* Run the test */
		ehandler = sieve_stderr_ehandler_create(0);
		ret = testsuite_run(sbin, &testsuite_msgdata, &scriptenv, ehandler);
		sieve_error_handler_unref(&ehandler);

		switch ( ret ) {
		case SIEVE_EXEC_OK:
			break;
		case SIEVE_EXEC_FAILURE:
		case SIEVE_EXEC_KEEP_FAILED:
			testsuite_testcase_fail("execution aborted");
			break;
		case SIEVE_EXEC_BIN_CORRUPT:
			testsuite_testcase_fail("binary corrupt");
			break;
		default:
			testsuite_testcase_fail("unknown execution exit code");
		}

		sieve_close(&sbin);

		if ( scriptenv.trace_stream != NULL )
			o_stream_unref(&scriptenv.trace_stream);

		/* De-initialize message environment */
		testsuite_message_deinit();
		testsuite_result_deinit();

		/* De-initialize mail user */
		if ( mail_user != NULL )
            mail_user_unref(&mail_user);

        mail_storage_service_deinit_user();
	} else {
		testsuite_testcase_fail("failed to compile testcase script");
	}

	/* De-initialize testsuite */
	testsuite_tool_deinit();  

	master_service_deinit(&service);

	return testsuite_testcase_result();
}
