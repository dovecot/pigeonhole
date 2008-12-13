/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __SIEVE_EXT_ENOTIFY_H
#define __SIEVE_EXT_ENOTIFY_H

#include "lib.h"

#include "sieve-common.h"

/*
 * Forward declarations
 */
 
struct sieve_enotify_context;

/*
 * Notify methods
 */ 

struct sieve_enotify_method {
	const char *identifier;
	
	/* Validation */
	bool (*validate_uri)
		(struct sieve_validator *valdtr, struct sieve_ast_argument *arg,
			const char *uri_body);

	/* Runtime */
	bool (*runtime_check_operands)
		(const struct sieve_runtime_env *renv, unsigned int source_line,
			const char *uri, const char *uri_body, const char *message, 
			const char *from, void **context);
			
	/* Action print */
	void (*action_print)
		(const struct sieve_result_print_env *rpenv, 
			const struct sieve_enotify_context *nctx);	
			
	/* Action execution */
	bool (*action_execute)
		(const struct sieve_action_exec_env *aenv, 
			const struct sieve_enotify_context *nctx);
};

void sieve_enotify_method_register(const struct sieve_enotify_method *method);

/*
 * Action context
 */
 
struct sieve_enotify_context {
	const struct sieve_enotify_method *method;
	void *method_context;
	
	sieve_number_t importance;
	const char *message;
	const char *from;
};


#endif /* __SIEVE_EXT_ENOTIFY_H */

