/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __SIEVE_ERROR_PRIVATE_H
#define __SIEVE_ERROR_PRIVATE_H

#include "sieve-error.h"

/*
 * Error handler object
 */

struct sieve_error_handler {
	pool_t pool;
	int refcount;
	
	unsigned int max_errors;

	unsigned int errors;
	unsigned int warnings;

	/* Should we copy log to i_error, i_warning and i_info? */
	bool log_master;

	/* Should the errorhandler handle or discard info log?
	 * (This does not influence the previous setting)
	 */
	bool log_info;

	void (*verror)
		(struct sieve_error_handler *ehandler, const char *location,
			const char *fmt, va_list args);
	void (*vwarning)
		(struct sieve_error_handler *ehandler, const char *location,
			const char *fmt, va_list args);
	void (*vinfo)
		(struct sieve_error_handler *ehandler, const char *location,
			const char *fmt, va_list args);

	void (*free)
		(struct sieve_error_handler *ehandler);
};

void sieve_error_handler_init
	(struct sieve_error_handler *ehandler, pool_t pool, unsigned int max_errors);

#endif /* __SIEVE_ERROR_PRIVATE_H */
