/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
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
#include "testsuite-message.h"

#include "testsuite-result.h"

static struct sieve_result *_testsuite_result;

void testsuite_result_init(void)
{
	_testsuite_result = sieve_result_create
		(sieve_instance, &testsuite_msgdata, testsuite_scriptenv, 
			testsuite_log_ehandler);
}

void testsuite_result_deinit(void)
{
	if ( _testsuite_result != NULL ) {
		sieve_result_unref(&_testsuite_result);
	}
}

void testsuite_result_reset
(const struct sieve_runtime_env *renv)
{
	if ( _testsuite_result != NULL ) {
		sieve_result_unref(&_testsuite_result);
	}

	_testsuite_result = sieve_result_create
		(sieve_instance, &testsuite_msgdata, testsuite_scriptenv, 
		testsuite_log_ehandler);
	sieve_interpreter_set_result(renv->interp, _testsuite_result);
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
	int ret;

	if ( _testsuite_result == NULL ) {
		sieve_runtime_error(renv, sieve_error_script_location(renv->script,0),
			"testsuite: no result evaluated yet");
		return FALSE;
	}

	testsuite_log_clear_messages();

	/* Execute the result */	
	ret=sieve_result_execute(_testsuite_result, NULL);
	
	return ( ret > 0 );
}

void testsuite_result_print
(const struct sieve_runtime_env *renv)
{
	struct ostream *out;
	
	out = o_stream_create_fd(1, 0, FALSE);	

	o_stream_send_str(out, "\n--");
	sieve_result_print(_testsuite_result, renv->scriptenv, out, NULL);
	o_stream_send_str(out, "--\n\n");

	o_stream_destroy(&out);	
}


