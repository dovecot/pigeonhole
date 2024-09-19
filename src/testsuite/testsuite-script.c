/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve.h"
#include "sieve-common.h"
#include "sieve-script.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"
#include "sieve-runtime-trace.h"
#include "sieve-result.h"

#include "testsuite-common.h"
#include "testsuite-settings.h"
#include "testsuite-log.h"
#include "testsuite-smtp.h"
#include "testsuite-result.h"

#include "testsuite-script.h"

/*
 * Tested script environment
 */

void testsuite_script_init(void)
{
}

void testsuite_script_deinit(void)
{
}

static const char *path_get_filename(const char *path)
{
	const char *filename;

	filename = strrchr(path, '/');
	if (filename == NULL)
		filename = path;
	else
		filename++;
	return filename;
}

const char *testsuite_script_get_name(const char *path)
{
	const char *file, *ext;

	file = path_get_filename(path);

	/* Extract the script name */
	ext = strrchr(file, '.');
	if (ext == NULL || ext == file ||
	    (strcmp(ext, ".svtest") != 0 &&
	     strcmp(ext, "."SIEVE_SCRIPT_FILEEXT) != 0))
		return NULL;

	return t_strdup_until(file, ext);
}

static struct sieve_binary *
_testsuite_script_compile(const struct sieve_runtime_env *renv,
			  const char *script)
{
	struct sieve_instance *svinst = testsuite_sieve_instance;
	struct sieve_binary *sbin;
	const char *script_path;

	sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS,
			    "compile script '%s'", script);

	script_path = sieve_file_script_get_dir_path(renv->script);
	if (script_path == NULL)
		return NULL;

	script_path = t_strconcat(script_path, "/", script, NULL);
	if (sieve_compile(svinst, script_path, NULL,
			  testsuite_log_ehandler, 0, &sbin, NULL) < 0)
		return NULL;

	return sbin;
}

bool testsuite_script_compile(const struct sieve_runtime_env *renv,
			      const char *script)
{
	struct testsuite_interpreter_context *ictx =
		testsuite_interpreter_context_get(renv->interp, testsuite_ext);
	struct sieve_binary *sbin;

	i_assert(ictx != NULL);
	testsuite_log_clear_messages();

	sbin = _testsuite_script_compile(renv, script);
	if (sbin == NULL)
		return FALSE;

	sieve_binary_unref(&ictx->compiled_script);

	ictx->compiled_script = sbin;
	return TRUE;
}

bool testsuite_script_is_subtest(const struct sieve_runtime_env *renv)
{
	struct testsuite_interpreter_context *ictx =
		testsuite_interpreter_context_get(renv->interp, testsuite_ext);

	i_assert(ictx != NULL);
	if (ictx->compiled_script == NULL)
		return FALSE;

	return (sieve_binary_extension_get_index(ictx->compiled_script,
						 testsuite_ext) >= 0);
}

bool testsuite_script_run(const struct sieve_runtime_env *renv)
{
	const struct sieve_execute_env *eenv = renv->exec_env;
	const struct sieve_script_env *senv = eenv->scriptenv;
	struct testsuite_interpreter_context *ictx =
		testsuite_interpreter_context_get(renv->interp, testsuite_ext);
	struct sieve_script_env scriptenv;
	struct sieve_exec_status exec_status;
	struct sieve_result *result;
	struct sieve_interpreter *interp;
	pool_t pool;
	struct sieve_execute_env exec_env;
	const char *error;
	int ret;

	i_assert(ictx != NULL);

	if (ictx->compiled_script == NULL) {
		sieve_runtime_error(renv, NULL, "testsuite: "
			"trying to run script, but no script compiled yet");
		return FALSE;
	}

	testsuite_log_clear_messages();

	i_zero(&exec_status);

	/* Compose script execution environment */
	if (sieve_script_env_init(&scriptenv, senv->user, &error) < 0) {
		sieve_runtime_error(renv, NULL,	"testsuite: "
			"failed to initialize script execution: %s", error);
		return FALSE;
	}
	scriptenv.default_mailbox = "INBOX";
	scriptenv.smtp_start = testsuite_smtp_start;
	scriptenv.smtp_add_rcpt = testsuite_smtp_add_rcpt;
	scriptenv.smtp_send = testsuite_smtp_send;
	scriptenv.smtp_abort = testsuite_smtp_abort;
	scriptenv.smtp_finish = testsuite_smtp_finish;
	scriptenv.duplicate_mark = NULL;
	scriptenv.duplicate_check = NULL;
	scriptenv.trace_log = eenv->scriptenv->trace_log;
	scriptenv.trace_config = eenv->scriptenv->trace_config;

	result = testsuite_result_get();

	pool = pool_alloconly_create("sieve execution", 4096);
	sieve_execute_init(&exec_env, eenv->svinst, pool, eenv->msgdata,
			   &scriptenv, eenv->flags);
	pool_unref(&pool);

	/* Execute the script */
	interp = sieve_interpreter_create(ictx->compiled_script, NULL,
					  &exec_env, testsuite_log_ehandler);

	if (interp == NULL) {
		sieve_execute_deinit(&exec_env);
		return FALSE;
	}

	ret = sieve_interpreter_run(interp, result);
	sieve_interpreter_free(&interp);

	sieve_execute_finish(&exec_env, ret);
	sieve_execute_deinit(&exec_env);

	return (ret > 0 ||
		sieve_binary_extension_get_index(ictx->compiled_script,
						 testsuite_ext) >= 0);
}

struct sieve_binary *
testsuite_script_get_binary(const struct sieve_runtime_env *renv)
{
	struct testsuite_interpreter_context *ictx =
		testsuite_interpreter_context_get(renv->interp, testsuite_ext);

	i_assert(ictx != NULL);
	return ictx->compiled_script;
}

void testsuite_script_set_binary(const struct sieve_runtime_env *renv,
				 struct sieve_binary *sbin)
{
	struct testsuite_interpreter_context *ictx =
		testsuite_interpreter_context_get(renv->interp, testsuite_ext);

	i_assert(ictx != NULL);

	sieve_binary_unref(&ictx->compiled_script);

	ictx->compiled_script = sbin;
	sieve_binary_ref(sbin);
}

/*
 * Multiscript
 */

bool testsuite_script_multiscript(const struct sieve_runtime_env *renv,
				  ARRAY_TYPE (const_string) *scriptfiles)
{
	struct sieve_instance *svinst = testsuite_sieve_instance;
	const struct sieve_execute_env *eenv = renv->exec_env;
	const struct sieve_script_env *senv = eenv->scriptenv;
	struct sieve_script_env scriptenv;
	struct sieve_exec_status exec_status;
	struct sieve_multiscript *mscript;
	const char *const *scripts;
	const char *error;
	unsigned int count, i;
	bool more = TRUE;
	bool result = TRUE;

	testsuite_log_clear_messages();

	/* Compose script execution environment */
	if (sieve_script_env_init(&scriptenv, senv->user, &error) < 0) {
		sieve_runtime_error(renv, NULL,
			"testsuite: failed to initialize script execution: %s",
			error);
		return FALSE;
	}
	scriptenv.default_mailbox = "INBOX";
	scriptenv.smtp_start = testsuite_smtp_start;
	scriptenv.smtp_add_rcpt = testsuite_smtp_add_rcpt;
	scriptenv.smtp_send = testsuite_smtp_send;
	scriptenv.smtp_abort = testsuite_smtp_abort;
	scriptenv.smtp_finish = testsuite_smtp_finish;
	scriptenv.duplicate_mark = NULL;
	scriptenv.duplicate_check = NULL;
	scriptenv.trace_log = eenv->scriptenv->trace_log;
	scriptenv.trace_config = eenv->scriptenv->trace_config;
	scriptenv.exec_status = &exec_status;

	/* Start execution */

	mscript = sieve_multiscript_start_execute(svinst, eenv->msgdata,
						  &scriptenv);

	/* Execute scripts before main script */

	scripts = array_get(scriptfiles, &count);
	for (i = 0; i < count && more; i++) {
		struct sieve_binary *sbin = NULL;
		const char *script = scripts[i];

		/* Open */
		sbin = _testsuite_script_compile(renv, script);
		if (sbin == NULL) {
			result = FALSE;
			break;
		}

		/* Execute */

		sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS,
				    "run script '%s'", script);

		more = sieve_multiscript_run(mscript, sbin,
					     testsuite_log_ehandler,
					     testsuite_log_ehandler, 0);

		sieve_close(&sbin);
	}

	return (sieve_multiscript_finish(&mscript, testsuite_log_ehandler,
					 0, SIEVE_EXEC_OK) > 0 && result);
}
