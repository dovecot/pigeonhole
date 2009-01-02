/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __TESTSUITE_SUBSTITUTIONS_H
#define __TESTSUITE_SUBSTITUTIONS_H

#include "sieve-common.h"
#include "sieve-objects.h"

struct testsuite_substitution {
	struct sieve_object object;
	
	bool (*get_value)(const char *param, string_t **result);
};

const struct testsuite_substitution *testsuite_substitution_find
	(const char *identifier);

struct sieve_ast_argument *testsuite_substitution_argument_create
	(struct sieve_validator *validator, struct sieve_ast *ast, 
		unsigned int source_line, const char *substitution, const char *param);

#endif /* __TESTSUITE_SUBSTITUTIONS_H */
