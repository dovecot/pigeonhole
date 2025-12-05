/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "string.h"
#include "ostream.h"
#include "hash.h"
#include "safe-mkstemp.h"
#include "mail-storage.h"
#include "env-util.h"
#include "unlink-directory.h"

#include "mail-raw.h"

#include "sieve-common.h"
#include "sieve-code.h"
#include "sieve-message.h"
#include "sieve-commands.h"
#include "sieve-extensions.h"
#include "sieve-binary.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"
#include "sieve-dump.h"

#include "testsuite-common.h"
#include "testsuite-settings.h"
#include "testsuite-objects.h"
#include "testsuite-log.h"
#include "testsuite-script.h"
#include "testsuite-binary.h"
#include "testsuite-result.h"
#include "testsuite-smtp.h"

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

/*
 * Global data
 */

struct sieve_instance *testsuite_sieve_instance = NULL;
char *testsuite_test_path = NULL;
unsigned int test_failures;

unsigned int test_failures;

static struct sieve_interpreter *testsuite_interp = NULL;

/* Test context */

static string_t *test_name;
static sieve_size_t test_block_end;
static unsigned int test_index;

/* Extension */

const struct sieve_extension *testsuite_ext;

/*
 * Validator context
 */

bool testsuite_validator_context_initialize(struct sieve_validator *valdtr)
{
	pool_t pool = sieve_validator_pool(valdtr);
	struct testsuite_validator_context *ctx =
		p_new(pool, struct testsuite_validator_context, 1);

	/* Setup object registry */
	ctx->object_registrations =
		sieve_validator_object_registry_create(valdtr);
	testsuite_register_core_objects(ctx);

	sieve_validator_extension_set_context(valdtr, testsuite_ext, ctx);

	return TRUE;
}

struct testsuite_validator_context *
testsuite_validator_context_get(struct sieve_validator *valdtr)
{
	return (struct testsuite_validator_context *)
		sieve_validator_extension_get_context(valdtr, testsuite_ext);
}

/*
 * Generator context
 */

bool testsuite_generator_context_initialize(
	struct sieve_generator *gentr, const struct sieve_extension *this_ext)
{
	pool_t pool = sieve_generator_pool(gentr);
	struct sieve_binary_block *sblock = sieve_generator_get_block(gentr);
	struct testsuite_generator_context *ctx =
		p_new(pool, struct testsuite_generator_context, 1);

	/* Setup exit jumplist */
	ctx->exit_jumps = sieve_jumplist_create(pool, sblock);

	sieve_generator_extension_set_context(gentr, this_ext, ctx);

	return TRUE;
}

/*
 * Interpreter context
 */

static void
testsuite_interpreter_free(const struct sieve_extension *ext ATTR_UNUSED,
			   struct sieve_interpreter *interp ATTR_UNUSED,
			   void *context)
{
	struct testsuite_interpreter_context *ctx =
		(struct testsuite_interpreter_context *)context;

	sieve_binary_unref(&ctx->compiled_script);
}

const struct sieve_interpreter_extension testsuite_interpreter_ext = {
	.ext_def = &testsuite_extension,
	.free = testsuite_interpreter_free,
};

bool testsuite_interpreter_context_initialize(
	struct sieve_interpreter *interp, const struct sieve_extension *this_ext)
{
	pool_t pool = sieve_interpreter_pool(interp);
	struct testsuite_interpreter_context *ctx =
		p_new(pool, struct testsuite_interpreter_context, 1);

	sieve_interpreter_extension_register(interp, this_ext,
					     &testsuite_interpreter_ext, ctx);
	return TRUE;
}

struct testsuite_interpreter_context *
testsuite_interpreter_context_get(struct sieve_interpreter *interp,
				  const struct sieve_extension *this_ext)
{
	struct testsuite_interpreter_context *ctx =
		sieve_interpreter_extension_get_context(interp, this_ext);

	return ctx;
}

/*
 * Test context
 */

static void testsuite_test_context_init(void)
{
	test_name = str_new(default_pool, 128);
	test_block_end = 0;
	test_index = 0;
	test_failures = 0;
}

int testsuite_test_start(const struct sieve_runtime_env *renv,
			 string_t *name, sieve_size_t block_end)
{
	if (test_block_end != 0) {
		sieve_runtime_trace_error(renv, "already inside test block");
		return SIEVE_EXEC_BIN_CORRUPT;
	}
	str_truncate(test_name, 0);
	str_append_str(test_name, name);

	test_block_end = block_end;
	test_index++;

	return SIEVE_EXEC_OK;
}

int testsuite_test_fail(const struct sieve_runtime_env *renv,
			string_t *reason)
{
	return testsuite_test_fail_cstr(renv, str_c(reason));
}

int testsuite_test_failf(const struct sieve_runtime_env *renv,
			 const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = testsuite_test_fail_cstr(renv, t_strdup_vprintf(fmt, args));
	va_end(args);

	return ret;
}

int testsuite_test_fail_cstr(const struct sieve_runtime_env *renv,
			     const char *reason)
{
	sieve_size_t end = test_block_end;

	if (str_len(test_name) == 0) {
		if (reason == NULL || *reason == '\0')
			printf("%2d: Test FAILED\n", test_index);
		else
			printf("%2d: Test FAILED: %s\n", test_index, reason);
	} else {
		if (reason == NULL || *reason == '\0') {
			printf("%2d: Test '%s' FAILED\n",
			       test_index, str_c(test_name));
		} else {
			printf("%2d: Test '%s' FAILED: %s\n",
			       test_index, str_c(test_name), reason);
		}
	}

	test_failures++;

	if (end == 0)
		return SIEVE_EXEC_FAILURE;
	if (renv->interp != testsuite_interp) {
		sieve_interpreter_interrupt(renv->interp);
		return SIEVE_EXEC_OK;
	}

	str_truncate(test_name, 0);
	test_block_end = 0;

	return sieve_interpreter_program_jump_to(renv->interp, end, TRUE);
}

void testsuite_testcase_fail(const char *reason)
{
	if (reason == NULL || *reason == '\0')
		printf("XX: Test CASE FAILED\n");
	else
		printf("XX: Test CASE FAILED: %s\n", reason);

	test_failures++;
}

int testsuite_test_succeed(const struct sieve_runtime_env *renv,
			   sieve_size_t *address, string_t *reason)
{
	sieve_size_t end = test_block_end;
	int ret;

	if (str_len(test_name) == 0) {
		if (reason == NULL || str_len(reason) == 0)
			printf("%2d: Test SUCCEEDED\n", test_index);
		else {
			printf("%2d: Test SUCCEEDED: %s\n",
			       test_index, str_c(reason));
		}
	} else {
		if (reason == NULL || str_len(reason) == 0) {
			printf("%2d: Test '%s' SUCCEEDED\n",
			       test_index, str_c(test_name));
		} else {
			printf("%2d: Test '%s' SUCCEEDED: %s\n", test_index,
				str_c(test_name), str_c(reason));
		}
	}

	str_truncate(test_name, 0);
	test_block_end = 0;

	if (*address > end) {
		sieve_runtime_trace_error(
			renv, "invalid test block end offset");
		return SIEVE_EXEC_BIN_CORRUPT;
	} else if (*address < end) {
		ret = sieve_interpreter_program_jump_to(
			renv->interp, end, FALSE);
		if (ret <= 0)
			return ret;
	}

	return SIEVE_EXEC_OK;
}

static void testsuite_test_context_deinit(void)
{
	str_free(&test_name);
}

bool testsuite_testcase_result(bool expect_failure)
{
	if (expect_failure) {
		if (test_failures < test_index) {
			printf("\nFAIL: Only %d of %d tests failed "
			       "(all expected to fail).\n\n",
			       test_failures, test_index);
			return FALSE;
		}

		printf("\nPASS: %d tests failed (expected to fail).\n\n",
		       (test_index == 0 ? 1 : test_index));
		return TRUE;
	}

	if (test_failures > 0) {
		printf("\nFAIL: %d of %d tests failed.\n\n",
		       test_failures, test_index);
		return FALSE;
	}

	printf("\nPASS: %d tests succeeded.\n\n", test_index);
	return TRUE;
}

/*
 * Testsuite temporary directory
 */

static char *testsuite_tmp_dir;

static void testsuite_tmp_dir_init(const char *tmp_path)
{
	if (tmp_path == NULL)
		tmp_path = "/tmp";

	string_t *dir = t_str_new(256);
	str_append(dir, tmp_path);
	str_append_c(dir, '/');
	str_append(dir, "sieve-testsuite");
	str_append_c(dir, '-');

	if (safe_mkstemp_dir_pid(dir, 0700) < 0)
		i_fatal("safe_mkstemp_dir(%s) failed: %m", str_c(dir));
	testsuite_tmp_dir = i_strdup(str_c(dir));
}

static void testsuite_tmp_dir_deinit(void)
{
	const char *error;

	if (unlink_directory(testsuite_tmp_dir,
			     UNLINK_DIRECTORY_FLAG_RMDIR, &error) < 0)
		i_warning("failed to remove temporary directory '%s': %s.",
			  testsuite_tmp_dir, error);

	i_free(testsuite_tmp_dir);
}

const char *testsuite_tmp_dir_get(void)
{
	return testsuite_tmp_dir;
}

/*
 * Main testsuite init/run/deinit
 */

void testsuite_init(struct sieve_instance *svinst, const char *test_path,
		    const char *wdir_path, bool log_stdout)
{
	int ret;

	testsuite_sieve_instance = svinst;

	testsuite_test_context_init();
	testsuite_log_init(log_stdout);
	testsuite_tmp_dir_init(wdir_path);

	testsuite_script_init();
	testsuite_binary_init();
	testsuite_smtp_init();

	ret = sieve_extension_register(svinst, &testsuite_extension, TRUE,
				       &testsuite_ext);
	i_assert(ret == 0);

	testsuite_test_path = i_strdup(test_path);
}

int testsuite_run(struct sieve_binary *sbin,
		  struct sieve_error_handler *ehandler)
{
	struct sieve_result *result;
	int ret = 0;

	/* Create the interpreter */
	testsuite_interp = sieve_interpreter_create(
		sbin, NULL, &testsuite_execute_env, ehandler);
	if (testsuite_interp == NULL)
		return SIEVE_EXEC_BIN_CORRUPT;

	/* Run the interpreter */
	result = testsuite_result_get();
	ret = sieve_interpreter_run(testsuite_interp, result);

	/* Free the interpreter */
	sieve_interpreter_free(&testsuite_interp);

	return ret;
}

void testsuite_deinit(void)
{
	i_free(testsuite_test_path);

	testsuite_smtp_deinit();
	testsuite_binary_deinit();
	testsuite_script_deinit();

	testsuite_tmp_dir_deinit();
	testsuite_log_deinit();
	testsuite_test_context_deinit();
}
