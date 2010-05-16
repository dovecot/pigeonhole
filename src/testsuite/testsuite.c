/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
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
#include "testsuite-settings.h"
#include "testsuite-result.h"
#include "testsuite-message.h"
#include "testsuite-smtp.h"
#include "testsuite-mailstore.h"

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
 * Testsuite Sieve environment
 */

static const struct sieve_environment testsuite_sieve_env = {
	sieve_tool_get_homedir,
	testsuite_setting_get
};

/*
 * Testsuite execution
 */

static void print_help(void)
{
	printf(
"Usage: testsuite [-t] [-E] [-d <dump filename>]\n"
"                 [-P <plugin>] [-x <extensions>]\n"
"                 <scriptfile>\n"
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
	if ( (interp=sieve_interpreter_create(sbin, msgdata, senv, ehandler)) == NULL )
		return SIEVE_EXEC_BIN_CORRUPT;

	/* Run the interpreter */
	result = testsuite_result_get();
	sieve_result_ref(result);
	ret = sieve_interpreter_run(interp, result);
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
	struct mail_storage_service_ctx *storage_service;
	struct mail_storage_service_user *service_user;
	struct mail_storage_service_input service_input;
	struct mail_user *mail_user_dovecot;
	const char *scriptfile, *dumpfile, *extensions; 
	const char *user, *home, *errstr;
	ARRAY_TYPE(const_string) plugins;
	struct sieve_binary *sbin;
	const char *sieve_dir;
	bool trace = FALSE, log_stdout = FALSE, debug = FALSE;
	int ret, c;

	master_service = master_service_init
		("testsuite", MASTER_SERVICE_FLAG_STANDALONE, &argc, &argv, "d:x:P:tED");

	user = getenv("USER");

	sieve_tool_init(FALSE);

	t_array_init(&plugins, 4);

	/* Parse arguments */
	scriptfile = dumpfile = extensions = NULL;

	while ((c = master_getopt(master_service)) > 0) {
		switch (c) {
		case 'd':
			/* destination address */
			dumpfile = optarg;
			break;
		case 'x':
			/* destination address */
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
		case 't':
			trace = TRUE;
			break;
		case 'E':
			log_stdout = TRUE;
			break;
		case 'D':
			debug = TRUE;
			break;
		default:
			print_help();
			i_fatal_status(EX_USAGE,
				"Unknown argument: %c", c);
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

	/* Initialize mail user */
	home = _get_cwd();
	user = sieve_tool_get_user();

	env_put("DOVECONF_ENV=1");
	env_put(t_strdup_printf("HOME=%s", home));
	env_put(t_strdup_printf("MAIL=maildir:/tmp/dovecot-test-%s", user));

	master_service_init_finish(master_service);

	memset(&service_input, 0, sizeof(service_input));
	service_input.module = "testsuite";
	service_input.service = "testsuite";
	service_input.username = user;

	storage_service = mail_storage_service_init
		(master_service, NULL, service_flags);
	if ( mail_storage_service_lookup_next(storage_service, &service_input,
		&service_user, &mail_user_dovecot, &errstr) <= 0 )
		i_fatal("%s", errstr);

	/* Initialize testsuite */
	testsuite_settings_init();

	sieve_tool_sieve_init(&testsuite_sieve_env, debug);
	sieve_tool_load_plugins(&plugins);
	sieve_extensions_set_string(sieve_instance, extensions);
	testsuite_init(sieve_instance, log_stdout);

	printf("Test case: %s:\n\n", scriptfile);

	/* Initialize environment */

	sieve_dir = strrchr(scriptfile, '/');
	if ( sieve_dir == NULL )
		sieve_dir= "./";
	else {
		sieve_dir = t_strdup_until(scriptfile, sieve_dir+1);
	}

	/* Currently needed for include (FIXME) */
	testsuite_setting_set
		("sieve_dir", t_strconcat(sieve_dir, "included", NULL));
	testsuite_setting_set
		("sieve_global_dir", t_strconcat(sieve_dir, "included-global", NULL));

	/* Compile sieve script */
	if ( (sbin = sieve_tool_script_compile(scriptfile, NULL)) != NULL ) {
		struct sieve_error_handler *ehandler;
		struct sieve_script_env scriptenv;

		/* Dump script */
		sieve_tool_dump_binary_to(sbin, dumpfile);
	
		testsuite_mailstore_init(user, home, mail_user_dovecot);

		if (master_service_set
			(master_service, "mail_full_filesystem_access=yes") < 0)
			i_unreached(); 

		testsuite_message_init(master_service, user, mail_user_dovecot);

		memset(&scriptenv, 0, sizeof(scriptenv));
		scriptenv.namespaces = testsuite_mailstore_get_namespace();
		scriptenv.default_mailbox = "INBOX";
		scriptenv.hostname = "testsuite.example.com";
		scriptenv.postmaster_address = "postmaster@example.com";
		scriptenv.username = user;
		scriptenv.smtp_open = testsuite_smtp_open;
		scriptenv.smtp_close = testsuite_smtp_close;
		scriptenv.trace_stream = ( trace ? o_stream_create_fd(1, 0, FALSE) : NULL );
		scriptenv.trace_level = SIEVE_TRLVL_TESTS;

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
		testsuite_mailstore_deinit();
		testsuite_result_deinit();
	} else {
		testsuite_testcase_fail("failed to compile testcase script");
	}

	/* De-initialize mail user */
	if ( mail_user_dovecot != NULL )
		mail_user_unref(&mail_user_dovecot);

	/* De-initialize testsuite */
	testsuite_deinit();	
	testsuite_settings_deinit();
	sieve_tool_deinit();

	mail_storage_service_user_free(&service_user);
	mail_storage_service_deinit(&storage_service);
	master_service_deinit(&master_service);

	if ( !testsuite_testcase_result() )
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
