/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __SIEVE_ERROR_H
#define __SIEVE_ERROR_H

#include "lib.h"
#include "compat.h"

#include <stdarg.h>

/*
 * Forward declarations
 */

struct sieve_script;
struct sieve_error_handler;

/*
 * Types
 */

typedef void (*sieve_error_vfunc_t)
	(struct sieve_error_handler *ehandler, const char *location, 
		const char *fmt, va_list args);
typedef void (*sieve_error_func_t)
	(struct sieve_error_handler *ehandler, const char *location, 
		const char *fmt, ...) ATTR_FORMAT(3, 4);

/*
 * System errors
 */

extern struct sieve_error_handler *_sieve_system_ehandler;

#define sieve_sys_error(...) sieve_error(_sieve_system_ehandler, NULL, __VA_ARGS__ )
#define sieve_sys_warning(...) sieve_warning(_sieve_system_ehandler, NULL, __VA_ARGS__ )
#define sieve_sys_info(...) sieve_info(_sieve_system_ehandler, NULL, __VA_ARGS__ )
#define sieve_sys_debug(...) sieve_debug(_sieve_system_ehandler, NULL, __VA_ARGS__ )

void sieve_system_ehandler_set(struct sieve_error_handler *ehandler);
void sieve_system_ehandler_reset(void);

/*
 * Main error functions
 */

/* For these functions it is the responsibility of the caller to
 * manage the datastack.
 */

const char *sieve_error_script_location
	(const struct sieve_script *script, unsigned int source_line);

void sieve_verror
	(struct sieve_error_handler *ehandler, const char *location, 
		const char *fmt, va_list args);
void sieve_vwarning
	(struct sieve_error_handler *ehandler, const char *location, 
		const char *fmt, va_list args); 
void sieve_vinfo
	(struct sieve_error_handler *ehandler, const char *location, 
		const char *fmt, va_list args); 
void sieve_vdebug
	(struct sieve_error_handler *ehandler, const char *location,
		const char *fmt, va_list args);
void sieve_vcritical
	(struct sieve_error_handler *ehandler, const char *location, 
		const char *fmt, va_list args);

void sieve_error
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, ...) ATTR_FORMAT(3, 4);
void sieve_warning
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, ...) ATTR_FORMAT(3, 4);
void sieve_info
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, ...) ATTR_FORMAT(3, 4);
void sieve_debug
(struct sieve_error_handler *ehandler, const char *location,
	const char *fmt, ...) ATTR_FORMAT(3, 4);
void sieve_critical
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, ...) ATTR_FORMAT(3, 4);

/*
 * Error handler configuration
 */

void sieve_error_handler_accept_infolog
	(struct sieve_error_handler *ehandler, bool enable);
void sieve_error_handler_accept_debuglog
	(struct sieve_error_handler *ehandler, bool enable);
void sieve_error_handler_copy_masterlog
	(struct sieve_error_handler *ehandler, bool enable);

/*
 * Error handler statistics
 */

unsigned int sieve_get_errors(struct sieve_error_handler *ehandler);
unsigned int sieve_get_warnings(struct sieve_error_handler *ehandler);

bool sieve_errors_more_allowed(struct sieve_error_handler *ehandler);

/*
 * Error handler object
 */

void sieve_error_handler_ref(struct sieve_error_handler *ehandler);
void sieve_error_handler_unref(struct sieve_error_handler **ehandler);

void sieve_error_handler_reset(struct sieve_error_handler *ehandler);

/* 
 * Error handlers 
 */

/* Write errors to dovecot master log */
struct sieve_error_handler *sieve_master_ehandler_create
	(unsigned int max_errors);

/* Write errors to stderr */
struct sieve_error_handler *sieve_stderr_ehandler_create
	(unsigned int max_errors);

/* Write errors into a string buffer */
struct sieve_error_handler *sieve_strbuf_ehandler_create
	(string_t *strbuf, bool crlf, unsigned int max_errors);

/* Write errors to a logfile */
struct sieve_error_handler *sieve_logfile_ehandler_create
	(const char *logfile, unsigned int max_errors);  

#endif /* __SIEVE_ERROR_H */
