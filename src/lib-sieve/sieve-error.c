#include <stdio.h>

#include "lib.h"

#include "sieve-error.h"

/* This should be moved to a sieve-errors-private.h when the need for other 
 * types of (externally defined) error handlers arises.
 */
struct sieve_error_handler {	
	pool_t pool;
	
	int errors;
	int warnings;
	
	void (*verror)
		(struct sieve_error_handler *ehandler, const char *location, 
			const char *fmt, va_list args);
	void (*vwarning)
		(struct sieve_error_handler *ehandler, const char *location, 
			const char *fmt, va_list args);
};

void sieve_verror
	(struct sieve_error_handler *ehandler, const char *location, 
		const char *fmt, va_list args)
{
	ehandler->verror(ehandler, location, fmt, args);
	ehandler->errors++;
}

void sieve_vwarning
	(struct sieve_error_handler *ehandler, const char *location, 
		const char *fmt, va_list args)
{
	ehandler->vwarning(ehandler, location, fmt, args);
	ehandler->warnings++;
}

unsigned int sieve_get_errors(struct sieve_error_handler *ehandler) {
	return ehandler->errors;
}

unsigned int sieve_get_warnings(struct sieve_error_handler *ehandler) {
	return ehandler->errors;
}

void sieve_error_handler_free(struct sieve_error_handler **ehandler)
{
	pool_t pool;
	
	if ( *ehandler != NULL ) {
		pool = (*ehandler)->pool;
		pool_unref(&pool);
	
		if ( pool == NULL )
			*ehandler = NULL;
	}
}

/* Output errors directly to stderror */

static void sieve_stderr_verror
(struct sieve_error_handler *ehandler ATTR_UNUSED, const char *location, 
	const char *fmt, va_list args) 
{
	fprintf(stderr, "%s: error: %s.\n", location, t_strdup_vprintf(fmt, args));
}

static void sieve_stderr_vwarning
(struct sieve_error_handler *ehandler ATTR_UNUSED, const char *location, 
	const char *fmt, va_list args) 
{
	fprintf(stderr, "%s: warning: %s.\n", location, t_strdup_vprintf(fmt, args));
}

struct sieve_error_handler *sieve_stderr_ehandler_create( void ) 
{
	pool_t pool;
	struct sieve_error_handler *ehandler;
	
	/* Pool is not strictly necessary, but other hander types will need a pool,
	 * so this one will have one too.
	 */
	pool = pool_alloconly_create
		("stderr_error_handler", sizeof(struct sieve_error_handler));	
	ehandler = p_new(pool, struct sieve_error_handler, 1);
	ehandler->pool = pool;
	ehandler->errors = 0;
	ehandler->warnings = 0;
	ehandler->verror = sieve_stderr_verror;
	ehandler->vwarning = sieve_stderr_vwarning;
	
	return ehandler;	
}

