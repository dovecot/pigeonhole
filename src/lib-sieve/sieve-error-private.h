#ifndef SIEVE_ERROR_PRIVATE_H
#define SIEVE_ERROR_PRIVATE_H

#include "sieve-error.h"

/*
 * Initialization
 */

void sieve_errors_init(struct sieve_instance *svinst);
void sieve_errors_deinit(struct sieve_instance *svinst);

/*
 * Error handler object
 */

struct sieve_error_handler {
	pool_t pool;
	int refcount;

	struct sieve_instance *svinst;

	unsigned int max_errors;

	unsigned int errors;
	unsigned int warnings;

	void (*log)(struct sieve_error_handler *ehandler,
		    const struct sieve_error_params *params,
		    enum sieve_error_flags flags,
		    const char *message);

	void (*free)(struct sieve_error_handler *ehandler);

	bool master_log:1;   /* this logs through master log facility */
	bool log_info:1;     /* handle or discard info log */
	bool log_debug:1;    /* handle or discard debug log */
};

void sieve_error_handler_init(struct sieve_error_handler *ehandler,
			      struct sieve_instance *svinst, pool_t pool,
			      unsigned int max_errors);

/*
 * Direct handler calls
 */

void sieve_direct_logv(struct sieve_instance *svinst,
		       struct sieve_error_handler *ehandler,
		       const struct sieve_error_params *params,
		       enum sieve_error_flags flags,
		       const char *fmt, va_list args) ATTR_FORMAT(5, 0);

static inline void ATTR_FORMAT(5, 6)
sieve_direct_log(struct sieve_instance *svinst,
		 struct sieve_error_handler *ehandler,
		 const struct sieve_error_params *params,
		 enum sieve_error_flags flags, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	sieve_direct_logv(svinst, ehandler, params, flags, fmt, args);
	va_end(args);
}

#endif
