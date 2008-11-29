/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __SIEVE_EXT_ENOTIFY_H
#define __SIEVE_EXT_ENOTIFY_H

#include "lib.h"

#include "sieve-common.h"

struct sieve_enotify_context {
	const char *method_uri;

	sieve_number_t importance;
	const char *message;
	const char *from;
	const char *const *options;
};

/*
 * Notify methods
 */ 

struct sieve_enotify_method {
	const char *identifier;
	
	/* Validation */
	bool (*validate_uri)
		(struct sieve_validator *valdtr, struct sieve_ast_argument *arg,
			const char *uri);
			
	/* Execution */
	bool (*execute)
		(const struct sieve_action_exec_env *aenv, 
			const struct sieve_enotify_context *nctx);
};

void sieve_enotify_method_register(const struct sieve_enotify_method *method);

#endif /* __SIEVE_EXT_ENOTIFY_H */

