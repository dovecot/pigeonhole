#include <stdio.h>

#include "lib.h"

#include "sieve-error.h"

/* FIMXE: This error handling is just a stub for what it should be. 
 */

struct sieve_error_handler {	
	int errors;
	int warnings;
};

static struct sieve_error_handler default_error_handler;

struct sieve_error_handler *sieve_error_handler_create( void ) {
	default_error_handler.errors = 0;	
	default_error_handler.warnings = 0;
	
	return &default_error_handler;	
}

void sieve_error(struct sieve_error_handler *ehandler, unsigned int line, const char *fmt, ...) 
{
	va_list args;
	va_start(args, fmt);
	
	sieve_verror(ehandler, line, fmt, args);
	
	va_end(args);
}

void sieve_warning(struct sieve_error_handler *ehandler, unsigned int line, const char *fmt, ...) 
{
	va_list args;
	va_start(args, fmt);
	
	sieve_vwarning(ehandler, line, fmt, args);
	
	va_end(args);
}

void sieve_verror(struct sieve_error_handler *ehandler, unsigned int line, const char *fmt, va_list args) 
{
	const char *nfmt;
				
	/* FIXME: This seems cumbersome.. */
	t_push();
	nfmt = t_strdup_printf("%d: error: %s.\n", line, fmt);
	vprintf(nfmt, args);
 	t_pop();
  
	ehandler->errors++;
}

void sieve_vwarning(struct sieve_error_handler *ehandler, unsigned int line, const char *fmt, va_list args) 
{
	const char *nfmt;
				
	/* FIXME: This seems cumbersome.. */
	t_push();
	nfmt = t_strdup_printf("%d: warning: %s.\n", line, fmt);
	vprintf(nfmt, args);
	t_pop();
  
	ehandler->warnings++;
}

unsigned int sieve_get_errors(struct sieve_error_handler *ehandler) {
	return ehandler->errors;
}

unsigned int sieve_get_warnings(struct sieve_error_handler *ehandler) {
	return ehandler->errors;
}

