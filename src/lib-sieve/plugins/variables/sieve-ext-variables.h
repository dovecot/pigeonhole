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
	
struct sieve_variable_storage;

struct sieve_variable_storage *sieve_variable_storage_create(pool_t pool);
void sieve_variable_get
	(struct sieve_variable_storage *storage, unsigned int index, 
		string_t **value);
void sieve_variable_assign
	(struct sieve_variable_storage *storage, unsigned int index, 
		const string_t *value);

#endif /* __SIEVE_EXT_VARIABLES_H */
