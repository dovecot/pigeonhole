/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-actions.h"
#include "sieve-result.h"

#include "testsuite-common.h"
#include "testsuite-result.h"

unsigned int _testsuite_result_index; /* Yuck */
static struct sieve_result *_testsuite_result;

void testsuite_result_init(void)
{
	_testsuite_result = NULL;
	_testsuite_result_index = 0;
}

void testsuite_result_deinit(void)
{
	if ( _testsuite_result != NULL ) {
		sieve_result_unref(&_testsuite_result);
	}
}

void testsuite_result_assign(struct sieve_result *result)
{
	if ( _testsuite_result != NULL ) {
		sieve_result_unref(&_testsuite_result);
	}

	_testsuite_result = result;
}

struct sieve_result_iterate_context *testsuite_result_iterate_init(void)
{
	if ( _testsuite_result == NULL )
		return NULL;

	return sieve_result_iterate_init(_testsuite_result);
}

