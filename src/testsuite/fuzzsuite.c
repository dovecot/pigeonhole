/* Copyright (c) 2020-2025 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "fuzzer.h"
#include "lib-signals.h"
#include "ioloop.h"
#include "env-util.h"
#include "istream.h"
#include "hostpid.h"
#include "settings.h"

#include "sieve.h"
#include "sieve-extensions.h"
#include "sieve-script.h"
#include "sieve-storage.h"
#include "sieve-binary.h"
#include "sieve-result.h"
#include "sieve-interpreter.h"

#include "sieve-settings.h"

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

//#define FUZZSUITE_DEBUG

const struct sieve_script_env *testsuite_scriptenv;

/*
 * Configuration
 */

extern const struct setting_parser_info ext_duplicate_setting_parser_info;
extern const struct setting_parser_info ext_editheader_header_setting_parser_info;
extern const struct setting_parser_info ext_editheader_setting_parser_info;
extern const struct setting_parser_info ext_extlists_list_setting_parser_info;
extern const struct setting_parser_info ext_extlists_setting_parser_info;
extern const struct setting_parser_info ext_include_setting_parser_info;
extern const struct setting_parser_info ext_spamtest_setting_parser_info;
extern const struct setting_parser_info ext_virustest_setting_parser_info;
extern const struct setting_parser_info ext_subaddress_setting_parser_info;
extern const struct setting_parser_info ext_vacation_setting_parser_info;
extern const struct setting_parser_info ext_variables_setting_parser_info;
extern const struct setting_parser_info ext_vnd_environment_setting_parser_info;
extern const struct setting_parser_info ext_report_setting_parser_info;
extern const struct setting_parser_info ntfy_mailto_setting_parser_info;
extern const struct setting_parser_info sieve_dict_storage_setting_parser_info;
extern const struct setting_parser_info sieve_file_storage_setting_parser_info;
extern const struct setting_parser_info sieve_setting_parser_info;
extern const struct setting_parser_info sieve_storage_setting_parser_info;

static const struct setting_parser_info *set_infos[] = {
	&ext_duplicate_setting_parser_info,
	&ext_editheader_header_setting_parser_info,
	&ext_editheader_setting_parser_info,
	&ext_extlists_list_setting_parser_info,
	&ext_extlists_setting_parser_info,
	&ext_include_setting_parser_info,
	&ext_spamtest_setting_parser_info,
	&ext_subaddress_setting_parser_info,
	&ext_vacation_setting_parser_info,
	&ext_variables_setting_parser_info,
	&ext_virustest_setting_parser_info,
	&ext_vnd_environment_setting_parser_info,
	&ext_report_setting_parser_info,
	&ntfy_mailto_setting_parser_info,

	&sieve_file_storage_setting_parser_info,
	&sieve_dict_storage_setting_parser_info,

	&sieve_setting_parser_info,
	&sieve_storage_setting_parser_info,
};
unsigned int set_infos_count = N_ELEMENTS(set_infos);

/*
 * Testsuite execution
 */

static void
fuzz_die(const siginfo_t *si ATTR_UNUSED, void *context ATTR_UNUSED)
{
	printf("COMMAND LINE INTERRUPT\n");

	testsuite_tmp_dir_deinit();

	/* No delays, no fuzzer reports: be gone already */
	_exit(0);
}

FUZZ_BEGIN_DATA(const unsigned char *data, size_t size)
{
	struct sieve_instance *svinst;
	struct sieve_binary *sbin;
	const char *sieve_dir = ".", *error;
	unsigned int i;
	bool log_stdout = FALSE;
	int ret;

	sieve_tool = sieve_tool_init_fuzzer("fuzzsuite");

	/* Allow quick command line termination when developing. This is not
	   expected in a normal fuzzer run. */
	lib_signals_set_handler(SIGINT, 0, fuzz_die, NULL);

#ifdef FUZZSUITE_DEBUG
	struct sieve_trace_config trace_config;

	i_zero(&trace_config);
	trace_config.level = SIEVE_TRLVL_MATCHING;

	log_stdout = TRUE;
#endif

	// FIXME: very very ugly
	master_service_parse_option(
		master_service, 'o',
		"postmaster_address=postmaster@example.com");
	master_service_parse_option(master_service, 'o', "mail_uid=");
	master_service_parse_option(master_service, 'o', "mail_gid=");

	/* Register settings manually */
	for (i = 0; i < set_infos_count; i++)
		settings_info_register(set_infos[i]);

	/* Finish testsuite initialization */
	svinst = sieve_tool_init_finish(sieve_tool, FALSE, FALSE);
#ifdef FUZZSUITE_DEBUG
	event_set_forced_debug(sieve_get_event(svinst), TRUE);
#endif
	testsuite_init(svinst, sieve_dir, sieve_tool_get_homedir(sieve_tool),
		       log_stdout);

	printf("Fuzz case:\n\n");

	struct settings_instance *set_instance =
		settings_instance_find(svinst->event);

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

	/* Construct Sieve script */
	struct sieve_script *script;
	struct istream *input;

	input = i_stream_create_from_data(data, size);
	script = sieve_data_script_create_from_input(
		svinst, SIEVE_SCRIPT_CAUSE_ANY, "fuzzsuite-main", input);

	/* Compile Sieve script */
	if (sieve_compile_script(script, testsuite_log_main_ehandler, 0,
				 &sbin, NULL) < 0) {
		testsuite_testcase_fail("failed to compile testcase script");
	} else {
#ifdef FUZZSUITE_DEBUG
		struct sieve_trace_log *trace_log = NULL;

		sieve_tool_dump_binary_to(sbin, "-", FALSE);
		(void)sieve_trace_log_create(svinst, NULL, &trace_log);
#endif

		struct sieve_exec_status exec_status;
		struct sieve_script_env scriptenv;

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
#ifdef FUZZSUITE_DEBUG
		scriptenv.trace_log = trace_log;
		scriptenv.trace_config = trace_config;
#endif
		scriptenv.exec_status = &exec_status;

		testsuite_scriptenv = &scriptenv;

		testsuite_result_init();

		/* Fuzz the test */
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
			i_panic("BUG: compiled test script binary is corrupt");
			break;
		case SIEVE_EXEC_RESOURCE_LIMIT:
			i_panic("BUG: resource limit exceeded");
			break;
		}

		sieve_close(&sbin);

		/* De-initialize message environment */
		testsuite_result_deinit();
		testsuite_message_deinit();
		testsuite_mailstore_deinit();

#ifdef FUZZSUITE_DEBUG
		if (trace_log != NULL)
			sieve_trace_log_free(&trace_log);
#endif

		testsuite_scriptenv = NULL;
	}

	sieve_script_unref(&script);
	i_stream_destroy(&input);

	/* De-initialize testsuite */
	testsuite_deinit();

	sieve_tool_deinit(&sieve_tool);

	testsuite_testcase_result(FALSE);
}
FUZZ_END
