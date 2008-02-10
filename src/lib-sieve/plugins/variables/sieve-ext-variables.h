#ifndef __SIEVE_EXT_VARIABLES_H
#define __SIEVE_EXT_VARIABLES_H

#include "sieve-common.h"
#include "sieve-extensions.h"

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

/* Extensions */

struct sieve_variables_extension {
	const struct sieve_extension *extension;
	
	struct sieve_extension_obj_registry set_modifiers;
};

#define SIEVE_EXT_DEFINE_SET_MODIFIER(OP) SIEVE_EXT_DEFINE_OBJECT(OP)
#define SIEVE_EXT_DEFINE_SET_MODIFIERS(OPS) SIEVE_EXT_DEFINE_OBJECTS(OPS)

void sieve_variables_extension_set
	(struct sieve_binary *sbin, int ext_id,
		const struct sieve_variables_extension *ext);

#endif /* __SIEVE_EXT_VARIABLES_H */
