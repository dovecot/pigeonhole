/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "lib-signals.h"
#include "ioloop.h"
#include "env-util.h"
#include "ostream.h"
#include "hostpid.h"
#include "path-util.h"
#include "settings.h"

#include "sieve.h"
#include "sieve-extensions.h"
#include "sieve-script.h"
#include "sieve-storage.h"
#include "sieve-binary.h"
#include "sieve-result.h"
#include "sieve-interpreter.h"

#include "sieve-tool.h"

#include "testsuite-common.h"
#include "testsuite-log.h"
#include "testsuite-settings.h"
#include "testsuite-result.h"
#include "testsuite-message.h"
#include "testsuite-script.h"
#include "testsuite-smtp.h"
#include "testsuite-mailstore.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <sysexits.h>

const struct sieve_script_env *testsuite_scriptenv;

/*
 * Configuration
 */

#define DEFAULT_SENDMAIL_PATH "/usr/lib/sendmail"

/*
 * Testsuite execution
 */

static void print_help(void)
{
	printf(
"Usage: testsuite [-D] [-E] [-F] [-d <dump-filename>]\n"
"                 [-t <trace-filename>] [-T <trace-option>]\n"
"                 [-P <plugin>] [-x <extensions>]\n"
"                 <scriptfile>\n"
	);
}

static int
testsuite_run(struct sieve_binary *sbin, struct sieve_error_handler *ehandler)
{
	struct sieve_interpreter *interp;
	struct sieve_result *result;
	int ret = 0;

	/* Create the interpreter */
	interp = sieve_interpreter_create(sbin, NULL, &testsuite_execute_env,
					  ehandler);
	if (interp == NULL)
		return SIEVE_EXEC_BIN_CORRUPT;

	/* Run the interpreter */
	result = testsuite_result_get();
	ret = sieve_interpreter_run(interp, result);

	/* Free the interpreter */
	sieve_interpreter_free(&interp);

	return ret;
}

int main(int argc, char **argv)
{
	struct sieve_instance *svinst;
	const char *abspath, *scriptfile, *dumpfile, *tracefile;
	struct sieve_trace_config trace_config;
	struct sieve_binary *sbin;
	const char *sieve_dir, *cwd, *error;
	bool log_stdout = FALSE, expect_failure = FALSE;
	int ret, c;

	sieve_tool = sieve_tool_init("testsuite", &argc, &argv,
				     "d:t:T:EF", TRUE);

	/* Parse arguments */
	dumpfile = tracefile = NULL;
	i_zero(&trace_config);
	trace_config.level = SIEVE_TRLVL_ACTIONS;
	while ((c = sieve_tool_getopt(sieve_tool)) > 0) {
		switch (c) {
		case 'd':
			/* destination address */
			dumpfile = optarg;
			break;
		case 't':
			/* trace file */
			tracefile = optarg;
			break;
		case 'T':
			sieve_tool_parse_trace_option(&trace_config, optarg);
			break;
		case 'E':
			log_stdout = TRUE;
			break;
		case 'F':
			expect_failure = TRUE;
			break;
		default:
			print_help();
			i_fatal_status(EX_USAGE, "Unknown argument: %c", c);
			break;
		}
	}

	if (optind < argc) {
		scriptfile = t_strdup(argv[optind++]);
	} else {
		print_help();
		i_fatal_status(EX_USAGE, "Missing <scriptfile> argument");
	}

	if (optind != argc) {
		print_help();
		i_fatal_status(EX_USAGE, "Unknown argument: %s", argv[optind]);
	}

	// FIXME: very very ugly
	master_service_parse_option(
		master_service, 'o',
		"postmaster_address=postmaster@example.com");
	master_service_parse_option(master_service, 'o', "mail_uid=");
	master_service_parse_option(master_service, 'o', "mail_gid=");

	/* Initialize mail user */
	if (t_get_working_dir(&cwd, &error) < 0)
		i_fatal("Failed to get working directory: %s", error);
	sieve_tool_set_homedir(sieve_tool, cwd);

	/* Manually setup the absolute sieve storage path for the executed
	   test script. */
	if (t_abspath(scriptfile, &abspath, &error) < 0)
		i_fatal("Failed to retrieve absolute path from test script: %s",
			error);
	sieve_dir = strrchr(abspath, '/');
	if (sieve_dir == NULL)
		sieve_dir = ".";
	else
		sieve_dir = t_strdup_until(abspath, sieve_dir);

	/* Finish testsuite initialization */
	svinst = sieve_tool_init_finish(sieve_tool, FALSE, FALSE);
	testsuite_init(svinst, sieve_dir, log_stdout);

	printf("Test case: %s:\n\n", scriptfile);

	struct settings_instance *set_instance =
		settings_instance_find(svinst->event);

	/* Configure main test script */
	settings_override(set_instance, "sieve_script+", "testsuite-main",
			  SETTINGS_OVERRIDE_TYPE_2ND_CLI_PARAM);
	settings_override(set_instance,
			  "sieve_script/testsuite-main/sieve_script_storage",
			  "testsuite-main",
			  SETTINGS_OVERRIDE_TYPE_2ND_CLI_PARAM);
	settings_override(set_instance,
			  "sieve_script/testsuite-main/sieve_script_name",
			  testsuite_script_get_name(scriptfile),
			  SETTINGS_OVERRIDE_TYPE_2ND_CLI_PARAM);
	settings_override(set_instance,
			  "sieve_script/testsuite-main/sieve_script_type",
			  "testsuite", SETTINGS_OVERRIDE_TYPE_2ND_CLI_PARAM);
	settings_override(set_instance,
			  "sieve_script/testsuite-main/sieve_script_driver",
			  "file", SETTINGS_OVERRIDE_TYPE_2ND_CLI_PARAM);
	settings_override(set_instance,
			  "sieve_script/testsuite-main/sieve_script_path",
			  scriptfile, SETTINGS_OVERRIDE_TYPE_2ND_CLI_PARAM);

	/* Configure personal storage */
	settings_override(set_instance, "sieve_script+", "included",
			  SETTINGS_OVERRIDE_TYPE_2ND_CLI_PARAM);
	settings_override(set_instance,
			  "sieve_script/included/sieve_script_storage",
			  "included",
			  SETTINGS_OVERRIDE_TYPE_2ND_CLI_PARAM);
	settings_override(set_instance,
			  "sieve_script/included/sieve_script_type",
			  SIEVE_STORAGE_TYPE_PERSONAL,
			  SETTINGS_OVERRIDE_TYPE_2ND_CLI_PARAM);
	settings_override(set_instance,
			  "sieve_script/included/sieve_script_driver",
			  "file", SETTINGS_OVERRIDE_TYPE_2ND_CLI_PARAM);
	settings_override(set_instance,
			  "sieve_script/included/sieve_script_path",
			  t_strdup_printf("%s/included", sieve_dir),
			  SETTINGS_OVERRIDE_TYPE_2ND_CLI_PARAM);

	/* Configure global storage */
	settings_override(set_instance, "sieve_script+", "included-global",
			  SETTINGS_OVERRIDE_TYPE_2ND_CLI_PARAM);
	settings_override(set_instance,
			  "sieve_script/included-global/sieve_script_storage",
			  "included-global",
			  SETTINGS_OVERRIDE_TYPE_2ND_CLI_PARAM);
	settings_override(set_instance,
			  "sieve_script/included-global/sieve_script_type",
			  SIEVE_STORAGE_TYPE_GLOBAL,
			  SETTINGS_OVERRIDE_TYPE_2ND_CLI_PARAM);
	settings_override(set_instance,
			  "sieve_script/included-global/sieve_script_driver",
			  "file", SETTINGS_OVERRIDE_TYPE_2ND_CLI_PARAM);
	settings_override(set_instance,
			  "sieve_script/included-global/sieve_script_path",
			  t_strdup_printf("%s/included-global", sieve_dir),
			  SETTINGS_OVERRIDE_TYPE_2ND_CLI_PARAM);

	/* Compile sieve script */
	if (sieve_compile(svinst, SIEVE_SCRIPT_CAUSE_ANY, "testsuite-main",
			  NULL, testsuite_log_main_ehandler, 0,
			  &sbin, NULL) < 0) {
		testsuite_testcase_fail("failed to compile testcase script");
	} else {
		struct sieve_trace_log *trace_log = NULL;
		struct sieve_exec_status exec_status;
		struct sieve_script_env scriptenv;

		/* Dump script */
		sieve_tool_dump_binary_to(sbin, dumpfile, FALSE);

		if (tracefile != NULL) {
			(void)sieve_trace_log_create(
				svinst, (strcmp(tracefile, "-") == 0 ?
					 NULL : tracefile), &trace_log);
		}

		testsuite_mailstore_init();
		testsuite_message_init();

		if (sieve_script_env_init(&scriptenv,
					  testsuite_mailstore_get_user(),
					  &error) < 0) {
			i_fatal("Failed to initialize script execution: %s",
				error);
		}

		i_zero(&exec_status);

		scriptenv.default_mailbox = "INBOX";
		scriptenv.smtp_start = testsuite_smtp_start;
		scriptenv.smtp_add_rcpt = testsuite_smtp_add_rcpt;
		scriptenv.smtp_send = testsuite_smtp_send;
		scriptenv.smtp_abort = testsuite_smtp_abort;
		scriptenv.smtp_finish = testsuite_smtp_finish;
		scriptenv.trace_log = trace_log;
		scriptenv.trace_config = trace_config;
		scriptenv.exec_status = &exec_status;

		testsuite_scriptenv = &scriptenv;

		testsuite_result_init();

		/* Run the test */
		ret = testsuite_run(sbin, testsuite_log_main_ehandler);

		switch (ret) {
		case SIEVE_EXEC_OK:
			break;
		case SIEVE_EXEC_FAILURE:
		case SIEVE_EXEC_KEEP_FAILED:
		case SIEVE_EXEC_TEMP_FAILURE:
			testsuite_testcase_fail(
				"test script execution aborted due to error");
			break;
		case SIEVE_EXEC_BIN_CORRUPT:
			testsuite_testcase_fail(
				"compiled test script binary is corrupt");
			break;
		case SIEVE_EXEC_RESOURCE_LIMIT:
			testsuite_testcase_fail(
				"resource limit exceeded");
			break;
		}

		sieve_close(&sbin);

		/* De-initialize message environment */
		testsuite_result_deinit();
		testsuite_message_deinit();
		testsuite_mailstore_deinit();

		if (trace_log != NULL)
			sieve_trace_log_free(&trace_log);

		testsuite_scriptenv = NULL;
	}

	/* De-initialize testsuite */
	testsuite_deinit();

	sieve_tool_deinit(&sieve_tool);

	if (!testsuite_testcase_result(expect_failure))
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
