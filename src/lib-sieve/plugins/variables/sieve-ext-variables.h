#ifndef __SIEVE_EXT_VARIABLES_H
#define __SIEVE_EXT_VARIABLES_H

/* Public interface for other extensions to use */

struct sieve_variable {
	const char *identifier;
	unsigned int index;
};

struct sieve_variable_scope;

struct sieve_variable_scope *sieve_variable_scope_create(pool_t pool);
struct sieve_variable *sieve_variable_scope_get_variable
	(struct sieve_variable_scope *scope, const char *identifier);


#endif /* __SIEVE_EXT_VARIABLES_H */
