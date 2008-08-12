/* 
 * Public interface for other extensions to use 
 */
 
#ifndef __SIEVE_EXT_VARIABLES_H
#define __SIEVE_EXT_VARIABLES_H

#include "sieve-common.h"
#include "sieve-extensions.h"
#include "sieve-objects.h"

#include "ext-variables-limits.h"

/*
 * Variable scope
 */

struct sieve_variable {
	const char *identifier;
	unsigned int index;

	const struct sieve_extension *ext;
	void *context;
};

struct sieve_variable_scope;

struct sieve_variable_scope *sieve_variable_scope_create
	(const struct sieve_extension *ext);
void sieve_variable_scope_ref
	(struct sieve_variable_scope *scope);
void sieve_variable_scope_unref
	(struct sieve_variable_scope **scope);
pool_t sieve_variable_scope_pool
	(struct sieve_variable_scope *scope);

struct sieve_variable *sieve_variable_scope_declare
	(struct sieve_variable_scope *scope, const char *identifier);
struct sieve_variable *sieve_variable_scope_import
	(struct sieve_variable_scope *scope, struct sieve_variable *var);
struct sieve_variable *sieve_variable_scope_get_variable
	(struct sieve_variable_scope *scope, const char *identifier, bool create);

/* Iteration over all declared variables */

struct sieve_variable_scope_iter;

struct sieve_variable_scope_iter *sieve_variable_scope_iterate_init
	(struct sieve_variable_scope *scope);
bool sieve_variable_scope_iterate
	(struct sieve_variable_scope_iter *iter, struct sieve_variable **var_r);
void sieve_variable_scope_iterate_deinit
	(struct sieve_variable_scope_iter **iter);

/* Statistics */

unsigned int sieve_variable_scope_declarations
	(struct sieve_variable_scope *scope);
unsigned int sieve_variable_scope_size
	(struct sieve_variable_scope *scope);

/* Get all native variables */

struct sieve_variable * const *sieve_variable_scope_get_variables
	(struct sieve_variable_scope *scope, unsigned int *size_r);


/* 
 * Variable storage
 */	
	
struct sieve_variable_storage;

struct sieve_variable_storage *sieve_variable_storage_create
	(pool_t pool, struct sieve_variable_scope *scope);
void sieve_variable_get
	(struct sieve_variable_storage *storage, unsigned int index, 
		string_t **value);
void sieve_variable_get_modifiable
	(struct sieve_variable_storage *storage, unsigned int index, 
		string_t **value);
void sieve_variable_assign
	(struct sieve_variable_storage *storage, unsigned int index, 
		const string_t *value);

/*
 * Variables access
 */

bool sieve_ext_variables_is_active(struct sieve_validator *valdtr);

struct sieve_variable_scope *sieve_ext_variables_get_main_scope
	(struct sieve_validator *validator);
	
struct sieve_variable_storage *sieve_ext_variables_get_storage
	(struct sieve_interpreter *interp, const struct sieve_extension *ext,
		bool create);
void sieve_ext_variables_set_storage
	(struct sieve_interpreter *interp, struct sieve_variable_storage *storage,
		const struct sieve_extension *ext);	
		
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

/* Modifiers */

struct sieve_variables_modifier {
	struct sieve_object object;
	
	unsigned int precedence;
	
	bool (*modify)(string_t *in, string_t **result);
};

#define SIEVE_VARIABLES_DEFINE_MODIFIER(OP) SIEVE_EXT_DEFINE_OBJECT(OP)
#define SIEVE_VARIABLES_DEFINE_MODIFIERS(OPS) SIEVE_EXT_DEFINE_OBJECTS(OPS)

void sieve_variables_modifier_register
(struct sieve_validator *valdtr, const struct sieve_variables_modifier *smodf);

#endif /* __SIEVE_EXT_VARIABLES_H */
