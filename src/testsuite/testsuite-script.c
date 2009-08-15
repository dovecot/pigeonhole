/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "env-util.h"

#include "sieve.h"
#include "sieve-common.h"
#include "sieve-script.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"

#include "testsuite-common.h"
#include "testsuite-log.h"
#include "testsuite-result.h"

#include "testsuite-script.h"

/*
 * Tested script environment
 */ 

struct sieve_binary *_testsuite_compiled_script;

void testsuite_script_init(void)
{
	_testsuite_compiled_script = NULL;
}

void testsuite_script_deinit(void)
{
	if ( _testsuite_compiled_script != NULL ) {
		sieve_binary_unref(&_testsuite_compiled_script);
	}
}

bool testsuite_script_compile(const char *script_path)
{
	struct sieve_binary *sbin;
	const char *sieve_dir;

	testsuite_log_clear_messages();

	/* Initialize environment */
	sieve_dir = strrchr(script_path, '/');
	if ( sieve_dir == NULL )
		sieve_dir= "./";
	else
		sieve_dir = t_strdup_until(script_path, sieve_dir+1);

	/* Currently needed for include (FIXME) */
	env_put(t_strconcat("SIEVE_DIR=", sieve_dir, "included", NULL));
	env_put(t_strconcat("SIEVE_GLOBAL_DIR=", sieve_dir, "included-global", NULL));

	if ( (sbin = sieve_compile(script_path, NULL, testsuite_log_ehandler)) == NULL )
		return FALSE;

	if ( _testsuite_compiled_script != NULL ) {
		sieve_binary_unref(&_testsuite_compiled_script);
	}

	_testsuite_compiled_script = sbin;

	return TRUE;
}

bool testsuite_script_run(const struct sieve_runtime_env *renv)
{
	struct sieve_script_env scriptenv;
	struct sieve_result *result;
	struct sieve_interpreter *interp;
	int ret;

	if ( _testsuite_compiled_script == NULL ) {
		sieve_runtime_error(renv, sieve_error_script_location(renv->script,0),
			"testsuite: no script compiled yet");
		return FALSE;
	}

	testsuite_log_clear_messages();

	/* Compose script execution environment */
	memset(&scriptenv, 0, sizeof(scriptenv));
	scriptenv.default_mailbox = "INBOX";
	scriptenv.namespaces = NULL;
	scriptenv.username = "user";
	scriptenv.hostname = "host.example.com";
	scriptenv.postmaster_address = "postmaster@example.com";
	scriptenv.smtp_open = NULL;
	scriptenv.smtp_close = NULL;
	scriptenv.duplicate_mark = NULL;
	scriptenv.duplicate_check = NULL;
	
	result = testsuite_result_get();

	/* Execute the script */
	interp=sieve_interpreter_create
		(_testsuite_compiled_script, testsuite_log_ehandler);
	
	if ( interp == NULL )
		return SIEVE_EXEC_BIN_CORRUPT;
		
	ret = sieve_interpreter_run
		(interp, renv->msgdata, &scriptenv, result);

	sieve_interpreter_free(&interp);

	return ( ret > 0 );
}

struct sieve_binary *testsuite_script_get_binary(void)
{
	return _testsuite_compiled_script;
}

void testsuite_script_set_binary(struct sieve_binary *sbin)
{
	if ( _testsuite_compiled_script != NULL ) {
		sieve_binary_unref(&_testsuite_compiled_script);
	}

	_testsuite_compiled_script = sbin;
	sieve_binary_ref(sbin);
}

