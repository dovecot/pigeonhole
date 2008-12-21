/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __SIEVE_EXT_ENOTIFY_H
#define __SIEVE_EXT_ENOTIFY_H

#include "lib.h"
#include "compat.h"
#include <stdarg.h>

#include "sieve-common.h"
#include "sieve-error.h"

/*
 * Forward declarations
 */

struct sieve_enotify_log_context;
struct sieve_enotify_context; 
struct sieve_enotify_action;
struct sieve_enotify_exec_env;

/*
 * Notify context
 */

struct sieve_enotify_context {
	struct sieve_error_handler *ehandler;
	
	/* Script location */
	const struct sieve_script *script;
	unsigned int source_line;

	const struct sieve_message_data *msgdata;
	pool_t pool;
};

/*
 * Notify methods
 */ 

struct sieve_enotify_method {
	const char *identifier;
	
	/* Validation */
	bool (*validate_uri)
		(const struct sieve_enotify_log_context *nctx, const char *uri, 
			const char *uri_body);

	/* Runtime */
	bool (*runtime_check_operands)
		(const struct sieve_enotify_log_context *nctx, const char *uri, 
			const char *uri_body, const char *message, const char *from, 
			pool_t context_pool, void **context);
			
	/* Action print */
	void (*action_print)
		(const struct sieve_result_print_env *rpenv, 
			const struct sieve_enotify_action *act);	
			
	/* Action execution */
	bool (*action_execute)
		(const struct sieve_enotify_exec_env *nenv, 
			const struct sieve_enotify_action *act);
};

void sieve_enotify_method_register(const struct sieve_enotify_method *method);

/*
 * Action context
 */
 
struct sieve_enotify_action {
	const struct sieve_enotify_method *method;
	void *method_context;
	
	sieve_number_t importance;
	const char *message;
	const char *from;
};

struct sieve_enotify_exec_env {
	const struct sieve_enotify_log_context *logctx;

	const struct sieve_script_env *scriptenv;
	const struct sieve_message_data *msgdata;
};

/*
 * Logging
 */

void sieve_enotify_error
	(const struct sieve_enotify_log_context *nlctx, const char *fmt, ...) 
		ATTR_FORMAT(2, 3);
void sieve_enotify_warning
	(const struct sieve_enotify_log_context *nlctx, const char *fmt, ...) 
		ATTR_FORMAT(2, 3);
void sieve_enotify_log
	(const struct sieve_enotify_log_context *nlctx, const char *fmt, ...) 
		ATTR_FORMAT(2, 3);

#endif /* __SIEVE_EXT_ENOTIFY_H */

