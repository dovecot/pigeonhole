#ifndef __SIEVE_ERROR_H__
#define __SIEVE_ERROR_H__

#include <stdarg.h>

struct sieve_error_handler;

struct sieve_error_handler *sieve_error_handler_create( void );

void sieve_error(struct sieve_error_handler *ehandler, unsigned int line, const char *fmt, ...);
void sieve_warning(struct sieve_error_handler *ehandler, unsigned int line, const char *fmt, ...);

void sieve_verror(struct sieve_error_handler *ehandler, unsigned int line, const char *fmt, va_list args);
void sieve_vwarning(struct sieve_error_handler *ehandler, unsigned int line, const char *fmt, va_list args); 

unsigned int sieve_get_errors(struct sieve_error_handler *ehandler);
unsigned int sieve_get_warnings(struct sieve_error_handler *ehandler);

struct sieve_error_handler *sieve_error_handler_create();

#endif /* __SIEVE_ERROR_H__ */
