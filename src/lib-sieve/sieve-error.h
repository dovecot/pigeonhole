#ifndef __SIEVE_ERROR_H
#define __SIEVE_ERROR_H

#include "lib.h"
#include "compat.h"

#include <stdarg.h>

struct sieve_error_handler;

void sieve_verror
	(struct sieve_error_handler *ehandler, const char *location, 
		const char *fmt, va_list args);
void sieve_vwarning
	(struct sieve_error_handler *ehandler, const char *location, 
		const char *fmt, va_list args); 
void sieve_vinfo
	(struct sieve_error_handler *ehandler, const char *location, 
		const char *fmt, va_list args); 

inline static void sieve_error
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, ...) ATTR_FORMAT(3, 4);
inline static void sieve_warning
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, ...) ATTR_FORMAT(3, 4);
inline static void sieve_info
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, ...) ATTR_FORMAT(3, 4);

inline static void sieve_error
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	
	sieve_verror(ehandler, location, fmt, args);
	
	va_end(args);
}

inline static void sieve_warning
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	
	sieve_vwarning(ehandler, location, fmt, args);
	
	va_end(args);
}

inline static void sieve_info
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	
	sieve_vinfo(ehandler, location, fmt, args);
	
	va_end(args);
}

void sieve_error_handler_accept_infolog
	(struct sieve_error_handler *ehandler, bool enable);
void sieve_error_handler_copy_masterlog
	(struct sieve_error_handler *ehandler, bool enable);

unsigned int sieve_get_errors(struct sieve_error_handler *ehandler);
unsigned int sieve_get_warnings(struct sieve_error_handler *ehandler);

void sieve_error_handler_free(struct sieve_error_handler **ehandler);

/* Error handlers */

struct sieve_error_handler *sieve_stderr_ehandler_create(void);
struct sieve_error_handler *sieve_logfile_ehandler_create(const char *logfile);  

#endif /* __SIEVE_ERROR_H */
