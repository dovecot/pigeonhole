/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve.h"
#include "sieve-common.h"
#include "sieve-settings.h"
#include "sieve-script.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"

#include "testsuite-common.h"
#include "testsuite-settings.h"
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

static struct sieve_binary *_testsuite_script_compile(const char *script_path)
{
	struct sieve_binary *sbin;
	const char *sieve_dir;

	/* Initialize environment */
	sieve_dir = strrchr(script_path, '/');
	if ( sieve_dir == NULL )
		sieve_dir= "./";
	else
		sieve_dir = t_strdup_until(script_path, sieve_dir+1);

	/* Currently needed for include (FIXME) */
	testsuite_setting_set
		("sieve_dir", t_strconcat(sieve_dir, "included", NULL));
	testsuite_setting_set
		("sieve_global_dir", t_strconcat(sieve_dir, "included-global", NULL));
	
	if ( (sbin = sieve_compile
		(sieve_instance, script_path, NULL, testsuite_log_ehandler)) == NULL )
		return NULL;

	return sbin;
}

bool testsuite_script_compile(const char *script_path)
{
	struct sieve_binary *sbin;

	testsuite_log_clear_messages();

	if ( (sbin=_testsuite_script_compile(script_path)) == NULL )
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
	scriptenv.username = "user";
	scriptenv.hostname = "host.example.com";
	scriptenv.postmaster_address = "postmaster@example.com";
	scriptenv.smtp_open = NULL;
	scriptenv.smtp_close = NULL;
	scriptenv.duplicate_mark = NULL;
	scriptenv.duplicate_check = NULL;
	scriptenv.namespaces = renv->scriptenv->namespaces;
	scriptenv.trace_stream = renv->scriptenv->trace_stream;
	
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

/*
 * Multiscript
 */

bool testsuite_script_multiscript
(const struct sieve_runtime_env *renv, ARRAY_TYPE (const_string) *scriptfiles)
{
	struct sieve_script_env scriptenv;
	struct sieve_multiscript *mscript;
	const char *const *scripts;
	unsigned int count, i;
	bool more = TRUE;
	int ret;

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
	scriptenv.namespaces = renv->scriptenv->namespaces;
	scriptenv.trace_stream = renv->scriptenv->trace_stream;	

	/* Start execution */

	mscript = sieve_multiscript_start_execute
		(sieve_instance, renv->msgdata, &scriptenv);

	/* Execute scripts before main script */

	scripts = array_get(scriptfiles, &count);

	for ( i = 0; i < count && more; i++ ) {
		struct sieve_binary *sbin = NULL;
		const char *script_path = scripts[i];
		bool final = ( i == count - 1 );

		/* Open */
	
		if ( (sbin=_testsuite_script_compile(script_path)) == NULL )
			break;

		/* Execute */

		more = sieve_multiscript_run(mscript, sbin, testsuite_log_ehandler, final);

		sieve_close(&sbin);
	}

	ret = sieve_multiscript_finish(&mscript, testsuite_log_ehandler, NULL);
	
	return ( ret > 0 );
}
