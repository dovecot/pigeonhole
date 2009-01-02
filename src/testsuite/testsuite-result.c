/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "ostream.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-actions.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"

#include "testsuite-common.h"
#include "testsuite-log.h"
#include "testsuite-result.h"

static struct sieve_result *_testsuite_result;

void testsuite_result_init(void)
{
	_testsuite_result = sieve_result_create(testsuite_log_ehandler);
}

void testsuite_result_deinit(void)
{
	if ( _testsuite_result != NULL ) {
		sieve_result_unref(&_testsuite_result);
	}
}

void testsuite_result_reset(void)
{
	if ( _testsuite_result != NULL ) {
		sieve_result_unref(&_testsuite_result);
	}

	_testsuite_result = sieve_result_create(testsuite_log_ehandler);;
}

struct sieve_result *testsuite_result_get(void)
{
	return _testsuite_result;
}

struct sieve_result_iterate_context *testsuite_result_iterate_init(void)
{
	if ( _testsuite_result == NULL )
		return NULL;

	return sieve_result_iterate_init(_testsuite_result);
}

bool testsuite_result_execute(const struct sieve_runtime_env *renv)
{
	struct sieve_script_env scriptenv;
	struct sieve_exec_status estatus;
	int ret;

	if ( _testsuite_result == NULL ) {
		sieve_runtime_error(renv, sieve_error_script_location(renv->script,0),
			"testsuite: no result evaluated yet");
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
	
	/* Execute the result */	
	ret=sieve_result_execute
		(_testsuite_result, renv->msgdata, &scriptenv, &estatus);
	
	return ( ret > 0 );
}

void testsuite_result_print
(const struct sieve_runtime_env *renv ATTR_UNUSED)
{
	struct ostream *out;
	
	out = o_stream_create_fd(1, 0, FALSE);	

	sieve_result_print(_testsuite_result, out);

	o_stream_destroy(&out);	
}


