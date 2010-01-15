/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
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

	/* Should we copy log to i_error, i_warning, i_info and i_debug? */
	bool log_master;

	/* Should the errorhandler handle or discard info/debug log?
	 * (This does not influence the previous setting)
	 */
	bool log_info;
	bool log_debug;

	sieve_error_vfunc_t verror;
	sieve_error_vfunc_t vwarning;
	sieve_error_vfunc_t vinfo;
	sieve_error_vfunc_t vdebug;

	void (*free)
		(struct sieve_error_handler *ehandler);
};

void sieve_error_handler_init
	(struct sieve_error_handler *ehandler, pool_t pool, unsigned int max_errors);

void sieve_error_handler_init_from_parent
	(struct sieve_error_handler *ehandler, pool_t pool, 
		struct sieve_error_handler *parent);

/*
 * Direct handler calls
 */

static inline void sieve_direct_verror
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, va_list args)
{
	if ( sieve_errors_more_allowed(ehandler) ) {
		if ( ehandler->verror != NULL )
			ehandler->verror(ehandler, location, fmt, args);
		
		if ( ehandler->pool != NULL )
			ehandler->errors++;
	}
}

static inline void sieve_direct_vwarning
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, va_list args)
{
	if ( ehandler->vwarning != NULL )	
		ehandler->vwarning(ehandler, location, fmt, args);

	if ( ehandler->pool != NULL )
		ehandler->warnings++;
}

static inline void sieve_direct_vinfo
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, va_list args)
{
	if ( ehandler->log_info && ehandler->vinfo != NULL )	
		ehandler->vinfo(ehandler, location, fmt, args);
}

static inline void sieve_direct_vdebug
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, va_list args)
{
	if ( ehandler->log_info && ehandler->vdebug != NULL )	
		ehandler->vdebug(ehandler, location, fmt, args);
}

static inline void sieve_direct_error
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	
	sieve_direct_verror(ehandler, location, fmt, args);
	
	va_end(args);
}

static inline void sieve_direct_warning
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	
	sieve_direct_vwarning(ehandler, location, fmt, args);
	
	va_end(args);
}

static inline void sieve_direct_info
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	
	sieve_direct_vinfo(ehandler, location, fmt, args);
	
	va_end(args);
}

static inline void sieve_direct_debug
(struct sieve_error_handler *ehandler, const char *location,
	const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	sieve_direct_vdebug(ehandler, location, fmt, args);

	va_end(args);
}


#endif /* __SIEVE_ERROR_PRIVATE_H */
