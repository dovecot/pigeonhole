#ifndef TESTSUITE_COMMON_H
#define TESTSUITE_COMMON_H

#include "sieve-common.h"

#include "sieve-tool.h"

/*
 * Global data
 */

extern struct sieve_instance *testsuite_sieve_instance;

extern const struct sieve_extension_def testsuite_extension;

extern const struct sieve_extension *testsuite_ext;

extern const struct sieve_script_env *testsuite_scriptenv;

extern char *testsuite_test_path;


/*
 * Validator context
 */

struct testsuite_validator_context {
	struct sieve_validator_object_registry *object_registrations;
};

bool testsuite_validator_context_initialize(struct sieve_validator *valdtr);
struct testsuite_validator_context *
testsuite_validator_context_get(struct sieve_validator *valdtr);

/*
 * Generator context
 */

struct testsuite_generator_context {
	struct sieve_jumplist *exit_jumps;
};

bool testsuite_generator_context_initialize(
	struct sieve_generator *gentr, const struct sieve_extension *this_ext);

/*
 * Interpreter context
 */

struct testsuite_interpreter_context {
	struct sieve_binary *compiled_script;
};

bool testsuite_interpreter_context_initialize(
	struct sieve_interpreter *interp,
	const struct sieve_extension *this_ext);
struct testsuite_interpreter_context *
testsuite_interpreter_context_get(struct sieve_interpreter *interp,
				  const struct sieve_extension *this_ext);

/*
 * Commands
 */

extern const struct sieve_command_def cmd_test;
extern const struct sieve_command_def cmd_test_fail;
extern const struct sieve_command_def cmd_test_config_set;
extern const struct sieve_command_def cmd_test_config_unset;
extern const struct sieve_command_def cmd_test_config_reload;
extern const struct sieve_command_def cmd_test_set;
extern const struct sieve_command_def cmd_test_result_reset;
extern const struct sieve_command_def cmd_test_result_print;
extern const struct sieve_command_def cmd_test_message;
extern const struct sieve_command_def cmd_test_message_print;
extern const struct sieve_command_def cmd_test_mailbox;
extern const struct sieve_command_def cmd_test_mailbox_create;
extern const struct sieve_command_def cmd_test_mailbox_delete;
extern const struct sieve_command_def cmd_test_binary_load;
extern const struct sieve_command_def cmd_test_binary_save;
extern const struct sieve_command_def cmd_test_imap_metadata_set;

/*
 * Tests
 */

extern const struct sieve_command_def tst_test_script_compile;
extern const struct sieve_command_def tst_test_script_run;
extern const struct sieve_command_def tst_test_multiscript;
extern const struct sieve_command_def tst_test_error;
extern const struct sieve_command_def tst_test_result_action;
extern const struct sieve_command_def tst_test_result_execute;

/*
 * Operations
 */

enum testsuite_operation_code {
	TESTSUITE_OPERATION_TEST,
	TESTSUITE_OPERATION_TEST_FINISH,
	TESTSUITE_OPERATION_TEST_FAIL,
	TESTSUITE_OPERATION_TEST_CONFIG_SET,
	TESTSUITE_OPERATION_TEST_CONFIG_UNSET,
	TESTSUITE_OPERATION_TEST_CONFIG_RELOAD,
	TESTSUITE_OPERATION_TEST_SET,
	TESTSUITE_OPERATION_TEST_SCRIPT_COMPILE,
	TESTSUITE_OPERATION_TEST_SCRIPT_RUN,
	TESTSUITE_OPERATION_TEST_MULTISCRIPT,
	TESTSUITE_OPERATION_TEST_ERROR,
	TESTSUITE_OPERATION_TEST_RESULT_ACTION,
	TESTSUITE_OPERATION_TEST_RESULT_EXECUTE,
	TESTSUITE_OPERATION_TEST_RESULT_RESET,
	TESTSUITE_OPERATION_TEST_RESULT_PRINT,
	TESTSUITE_OPERATION_TEST_MESSAGE_SMTP,
	TESTSUITE_OPERATION_TEST_MESSAGE_MAILBOX,
	TESTSUITE_OPERATION_TEST_MESSAGE_PRINT,
	TESTSUITE_OPERATION_TEST_MAILBOX_CREATE,
	TESTSUITE_OPERATION_TEST_MAILBOX_DELETE,
	TESTSUITE_OPERATION_TEST_BINARY_LOAD,
	TESTSUITE_OPERATION_TEST_BINARY_SAVE,
	TESTSUITE_OPERATION_TEST_IMAP_METADATA_SET
};

extern const struct sieve_operation_def test_operation;
extern const struct sieve_operation_def test_finish_operation;
extern const struct sieve_operation_def test_fail_operation;
extern const struct sieve_operation_def test_config_set_operation;
extern const struct sieve_operation_def test_config_unset_operation;
extern const struct sieve_operation_def test_config_reload_operation;
extern const struct sieve_operation_def test_set_operation;
extern const struct sieve_operation_def test_script_compile_operation;
extern const struct sieve_operation_def test_script_run_operation;
extern const struct sieve_operation_def test_multiscript_operation;
extern const struct sieve_operation_def test_error_operation;
extern const struct sieve_operation_def test_result_action_operation;
extern const struct sieve_operation_def test_result_execute_operation;
extern const struct sieve_operation_def test_result_reset_operation;
extern const struct sieve_operation_def test_result_print_operation;
extern const struct sieve_operation_def test_message_smtp_operation;
extern const struct sieve_operation_def test_message_mailbox_operation;
extern const struct sieve_operation_def test_message_print_operation;
extern const struct sieve_operation_def test_mailbox_create_operation;
extern const struct sieve_operation_def test_mailbox_delete_operation;
extern const struct sieve_operation_def test_binary_load_operation;
extern const struct sieve_operation_def test_binary_save_operation;
extern const struct sieve_operation_def test_imap_metadata_set_operation;

/*
 * Operands
 */

extern const struct sieve_operand_def testsuite_object_operand;
extern const struct sieve_operand_def testsuite_substitution_operand;

enum testsuite_operand_code {
	TESTSUITE_OPERAND_OBJECT,
	TESTSUITE_OPERAND_SUBSTITUTION,
	TESTSUITE_OPERAND_NAMESPACE
};

/*
 * Test context
 */

int testsuite_test_start(const struct sieve_runtime_env *renv,
			 string_t *name, sieve_size_t block_end);
int testsuite_test_fail(const struct sieve_runtime_env *renv,
			string_t *reason);
int testsuite_test_failf(const struct sieve_runtime_env *renv,
			 const char *fmt, ...) ATTR_FORMAT(2, 3);
int testsuite_test_fail_cstr(const struct sieve_runtime_env *renv,
			     const char *reason);

int testsuite_test_succeed(const struct sieve_runtime_env *renv,
			   sieve_size_t *address, string_t *reason);

void testsuite_testcase_fail(const char *reason);
bool testsuite_testcase_result(bool expect_failure);

/*
 * Testsuite temporary directory
 */

const char *testsuite_tmp_dir_get(void);

/*
 * Testsuite init/run/deinit
 */

void testsuite_init(struct sieve_instance *svinst, const char *test_path,
		    const char *wdir_path, bool log_stdout);
int testsuite_run(struct sieve_binary *sbin,
		  struct sieve_error_handler *ehandler);
void testsuite_deinit(void);

#endif
