#include "lib.h"
#include "array.h"

#include "sieve-common.h"
#include "sieve-error-private.h"

#include "testsuite-log.h"

/* 
 * Testsuite error handler
 */

struct sieve_error_handler *testsuite_log_ehandler = NULL;

struct _testsuite_log_message {
	const char *location;
	const char *message;
};

bool _testsuite_log_stdout = TRUE;

unsigned int _testsuite_log_error_index = 0;

static pool_t _testsuite_logmsg_pool = NULL;
ARRAY_DEFINE(_testsuite_log_errors, struct _testsuite_log_message);

static void _testsuite_log_verror
(struct sieve_error_handler *ehandler ATTR_UNUSED, const char *location,
	const char *fmt, va_list args)
{
	pool_t pool = _testsuite_logmsg_pool;
	struct _testsuite_log_message msg;

	if ( _testsuite_log_stdout ) 	
	{
		va_list args_copy;
		VA_COPY(args_copy, args);
		printf("error: %s: %s.\n", location, t_strdup_vprintf(fmt, args_copy));
	}
	
	msg.location = p_strdup(pool, location);
	msg.message = p_strdup_vprintf(pool, fmt, args);

	array_append(&_testsuite_log_errors, &msg, 1);	
}

static struct sieve_error_handler *_testsuite_log_ehandler_create(void)
{
	pool_t pool;
	struct sieve_error_handler *ehandler;

	/* Pool is not strictly necessary, but other handler types will need a pool,
	 * so this one will have one too.
	 */
	pool = pool_alloconly_create
		("testsuite_log_handler", sizeof(struct sieve_error_handler));
	ehandler = p_new(pool, struct sieve_error_handler, 1);
	sieve_error_handler_init(ehandler, pool, 0);

	ehandler->verror = _testsuite_log_verror;

	return ehandler;
}

void testsuite_log_clear_messages(void)
{
	if ( _testsuite_logmsg_pool != NULL ) {
		if ( array_count(&_testsuite_log_errors) == 0 )
			return;
		pool_unref(&_testsuite_logmsg_pool);
	}

	_testsuite_logmsg_pool = pool_alloconly_create
		("testsuite_log_messages", 8192);
	
	p_array_init(&_testsuite_log_errors, _testsuite_logmsg_pool, 128);	

	sieve_error_handler_reset(testsuite_log_ehandler);
}

void testsuite_log_get_error_init(void)
{
	_testsuite_log_error_index = 0;
}

const char *testsuite_log_get_error_next(bool location)
{
	const struct _testsuite_log_message *msg;

	if ( _testsuite_log_error_index >= array_count(&_testsuite_log_errors) )
		return NULL;

	msg = array_idx(&_testsuite_log_errors, _testsuite_log_error_index++);

	if ( location ) 
		return msg->location;

	return msg->message;		
}

void testsuite_log_init(void)
{
	testsuite_log_ehandler = _testsuite_log_ehandler_create(); 	
	sieve_error_handler_accept_infolog(testsuite_log_ehandler, TRUE);

	testsuite_log_clear_messages();
}

void testsuite_log_deinit(void)
{
	sieve_error_handler_unref(&testsuite_log_ehandler);

	pool_unref(&_testsuite_logmsg_pool);
}




