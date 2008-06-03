/* 
 * Public interface for other extensions to use 
 */
 
#ifndef __SIEVE_EXT_VARIABLES_H
#define __SIEVE_EXT_VARIABLES_H

#include "sieve-common.h"
#include "sieve-extensions.h"

/*
 * Variable scope
 */

struct sieve_variable {
	const char *identifier;
	unsigned int index;
	int ext_id;
};

struct sieve_variable_scope;

struct sieve_variable_scope *sieve_variable_scope_create
	(pool_t pool, int ext_id);
struct sieve_variable *sieve_variable_scope_declare
	(struct sieve_variable_scope *scope, const char *identifier);
struct sieve_variable *sieve_variable_scope_import
	(struct sieve_variable_scope *scope, struct sieve_variable *var);
struct sieve_variable *sieve_variable_scope_get_variable
	(struct sieve_variable_scope *scope, const char *identifier, bool create);
	
/* 
 * Variable storage
 */	
	
struct sieve_variable_storage;

struct sieve_variable_storage *sieve_variable_storage_create(pool_t pool);
void sieve_variable_get
	(struct sieve_variable_storage *storage, unsigned int index, 
		string_t **value);
void sieve_variable_assign
	(struct sieve_variable_storage *storage, unsigned int index, 
		const string_t *value);

/* 
 * Variable extensions 
 */

struct sieve_variables_extension {
	const struct sieve_extension *extension;
	
	struct sieve_extension_obj_registry set_modifiers;
};

#define SIEVE_EXT_DEFINE_SET_MODIFIER(OP) SIEVE_EXT_DEFINE_OBJECT(OP)
#define SIEVE_EXT_DEFINE_SET_MODIFIERS(OPS) SIEVE_EXT_DEFINE_OBJECTS(OPS)

void sieve_variables_extension_set
	(struct sieve_binary *sbin, int ext_id,
		const struct sieve_variables_extension *ext);

/*
 * Variables access
 */

bool sieve_ext_variables_is_active(struct sieve_validator *valdtr);

struct sieve_variable_scope *sieve_ext_variables_get_main_scope
	(struct sieve_validator *validator);
	
struct sieve_variable_storage *sieve_ext_variables_get_storage
	(struct sieve_interpreter *interp, int ext_id);
void sieve_ext_variables_set_storage
	(struct sieve_interpreter *interp, struct sieve_variable_storage *storage,
		int ext_id);	
		
/* Variable arguments */

bool sieve_variable_argument_activate
(struct sieve_validator *validator, struct sieve_command_context *cmd, 
	struct sieve_ast_argument *arg, bool assignment);
	
/* Variable operands */

extern const struct sieve_operand variable_operand;

bool sieve_variable_operand_read_data
	(const struct sieve_runtime_env *renv, const struct sieve_operand *operand, 
		sieve_size_t *address, struct sieve_variable_storage **storage, 
		unsigned int *var_index);
bool sieve_variable_operand_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address, 
		struct sieve_variable_storage **storage, unsigned int *var_index);
		
static inline bool sieve_operand_is_variable
(const struct sieve_operand *operand)
{
	return ( operand != NULL && operand == &variable_operand );
}	

#endif /* __SIEVE_EXT_VARIABLES_H */
