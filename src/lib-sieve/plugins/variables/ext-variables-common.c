#include "lib.h"
#include "hash.h"
#include "str.h"
#include "array.h"

#include "sieve-common.h"

#include "sieve-ast.h"
#include "sieve-binary.h"
#include "sieve-code.h"
#include "sieve-match-types.h"

#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-code-dumper.h"
#include "sieve-interpreter.h"

#include "ext-variables-common.h"
#include "ext-variables-name.h"

#include <ctype.h>

/* Forward declarations */

extern const struct ext_variables_set_modifier lower_modifier;
extern const struct ext_variables_set_modifier upper_modifier;
extern const struct ext_variables_set_modifier lowerfirst_modifier;
extern const struct ext_variables_set_modifier upperfirst_modifier;
extern const struct ext_variables_set_modifier quotewildcard_modifier;
extern const struct ext_variables_set_modifier length_modifier;

const struct ext_variables_set_modifier *default_set_modifiers[] = { 
	&lower_modifier, &upper_modifier, &lowerfirst_modifier, &upperfirst_modifier,
	&quotewildcard_modifier, &length_modifier
};

const unsigned int default_set_modifiers_count = 
	N_ELEMENTS(default_set_modifiers);
	
/*
 * Binary context
 */

const struct sieve_variables_extension *
	sieve_variables_extension_get(struct sieve_binary *sbin, int ext_id)
{
	return (const struct sieve_variables_extension *)
		sieve_binary_registry_get_object(sbin, ext_variables_my_id, ext_id);
}

void sieve_variables_extension_set
	(struct sieve_binary *sbin, int ext_id,
		const struct sieve_variables_extension *ext)
{
	sieve_binary_registry_set_object
		(sbin, ext_variables_my_id, ext_id, (const void *) ext);
}

/* Variable scope */

struct sieve_variable_scope {
	pool_t pool;
  const char *identifier;

	unsigned int next_index;
	struct hash_table *variables;
};

struct sieve_variable_scope *sieve_variable_scope_create(pool_t pool) 
{
	struct sieve_variable_scope *scope;
	
	scope = p_new(pool, struct sieve_variable_scope, 1);
	scope->pool = pool;
	scope->variables = hash_create
		(pool, pool, 0, strcase_hash, (hash_cmp_callback_t *)strcasecmp);
		
	return scope;
}

struct sieve_variable *sieve_variable_scope_declare
(struct sieve_variable_scope *scope, const char *identifier)
{
	struct sieve_variable *var = p_new(scope->pool, struct sieve_variable, 1);
	var->identifier = identifier;
	var->index = scope->next_index++;
		
	hash_insert(scope->variables, (void *) identifier, (void *) var);
	
	return var;
}

struct sieve_variable *sieve_variable_scope_get_variable
(struct sieve_variable_scope *scope, const char *identifier, bool declare)
{
	struct sieve_variable *var = 
		(struct sieve_variable *) hash_lookup(scope->variables, identifier);
	
	if ( var == NULL && declare )
		var = sieve_variable_scope_declare(scope, identifier);
	
	return var;
}

/* Variable storage */

struct sieve_variable_storage {
	pool_t pool;
	ARRAY_DEFINE(var_values, string_t *); 
};

struct sieve_variable_storage *sieve_variable_storage_create(pool_t pool)
{
	struct sieve_variable_storage *storage;
	
	storage = p_new(pool, struct sieve_variable_storage, 1);
	storage->pool = pool;
		
	p_array_init(&storage->var_values, pool, 4);

	return storage;
}

void sieve_variable_get
(struct sieve_variable_storage *storage, unsigned int index, string_t **value)
{
	*value = NULL;
	
	if  ( index < array_count(&storage->var_values) ) {
		string_t * const *varent;
			
		varent = array_idx(&storage->var_values, index);
		
		*value = *varent;
	};
} 

void sieve_variable_assign
(struct sieve_variable_storage *storage, unsigned int index, 
	const string_t *value)
{
	string_t *varval;
	
	sieve_variable_get(storage, index, &varval);
	
	if ( varval == NULL ) {
		varval = str_new(storage->pool, str_len(value));
		array_idx_set(&storage->var_values, index, &varval);	
	} else {
		str_truncate(varval, 0);
	}
	
	str_append_str(varval, value);
}

/* Validator context */

struct ext_variables_validator_context {
	struct hash_table *set_modifiers;
	
	struct sieve_variable_scope *main_scope;
};

static struct ext_variables_validator_context *
ext_variables_validator_context_create(struct sieve_validator *validator)
{		
	pool_t pool = sieve_validator_pool(validator);
	struct ext_variables_validator_context *ctx;
	struct sieve_ast *ast = sieve_validator_ast(validator);
	
	ctx = p_new(pool, struct ext_variables_validator_context, 1);
	ctx->set_modifiers = hash_create
		(pool, pool, 0, str_hash, (hash_cmp_callback_t *)strcmp);
	ctx->main_scope = sieve_variable_scope_create(sieve_ast_pool(ast));

	sieve_validator_extension_set_context
		(validator, ext_variables_my_id, (void *) ctx);

	return ctx;
}

void ext_variables_validator_initialize(struct sieve_validator *validator)
{
	unsigned int i;
	struct ext_variables_validator_context *ctx;
	
	/* Create our context */
	ctx = ext_variables_validator_context_create(validator);
	
	for ( i = 0; i < default_set_modifiers_count; i++ ) {
		const struct ext_variables_set_modifier *mod = default_set_modifiers[i];
	
		hash_insert(ctx->set_modifiers, (void *) mod->identifier, (void *) mod);
	}
}

static inline struct ext_variables_validator_context *
ext_variables_validator_context_get(struct sieve_validator *validator)
{
	return (struct ext_variables_validator_context *)
		sieve_validator_extension_get_context(validator, ext_variables_my_id);
}

struct sieve_variable *ext_variables_validator_get_variable
(struct sieve_validator *validator, const char *variable, bool declare)
{
	struct ext_variables_validator_context *ctx = 
		ext_variables_validator_context_get(validator);
		
	return sieve_variable_scope_get_variable(ctx->main_scope, variable, declare);
}

bool sieve_ext_variables_is_active(struct sieve_validator *valdtr)
{
	return ( ext_variables_validator_context_get(valdtr) != NULL );
}

/* Interpreter context */

struct ext_variables_interpreter_context {
	struct sieve_variable_storage *local_storage;
};

static struct ext_variables_interpreter_context *
ext_variables_interpreter_context_create(struct sieve_interpreter *interp)
{		
	pool_t pool = sieve_interpreter_pool(interp);
	struct ext_variables_interpreter_context *ctx;
	
	ctx = p_new(pool, struct ext_variables_interpreter_context, 1);
	ctx->local_storage = sieve_variable_storage_create(pool);

	sieve_interpreter_extension_set_context
		(interp, ext_variables_my_id, (void *) ctx);

	return ctx;
}

void ext_variables_interpreter_initialize(struct sieve_interpreter *interp)
{
	struct ext_variables_interpreter_context *ctx;
	
	/* Create our context */
	ctx = ext_variables_interpreter_context_create(interp);
	
	/* Enable support for match values */
	(void) sieve_match_values_set_enabled(interp, TRUE);
}

static inline struct ext_variables_interpreter_context *
ext_variables_interpreter_context_get(struct sieve_interpreter *interp)
{
	return (struct ext_variables_interpreter_context *)
		sieve_interpreter_extension_get_context(interp, ext_variables_my_id);
}

struct sieve_variable_storage *ext_variables_interpreter_get_storage
	(struct sieve_interpreter *interp)
{
	struct ext_variables_interpreter_context *ctx = 
		ext_variables_interpreter_context_get(interp);
		
	return ctx->local_storage;
}

/* 
 * Operands 
 */

/* Variable operand */

static bool opr_variable_read_value
	(const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str);
static bool opr_variable_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);

const struct sieve_opr_string_interface variable_interface = { 
	opr_variable_dump,
	opr_variable_read_value
};
		
const struct sieve_operand variable_operand = { 
	"variable", 
	&variables_extension, 
	EXT_VARIABLES_OPERAND_VARIABLE,
	&string_class,
	&variable_interface
};	

void ext_variables_opr_variable_emit
	(struct sieve_binary *sbin, struct sieve_variable *var) 
{
	(void) sieve_operand_emit_code(sbin, &variable_operand, ext_variables_my_id);
	(void) sieve_binary_emit_integer(sbin, var->index);
}

static bool opr_variable_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address) 
{
	sieve_size_t index = 0;
	
	if (sieve_binary_read_integer(denv->sbin, address, &index) ) {
		sieve_code_dumpf(denv, "VAR: %ld", (long) index);

		return TRUE;
	}
	
	return FALSE;
}

static bool opr_variable_read_value
  (const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str)
{ 
	struct sieve_variable_storage *storage;
	sieve_size_t index = 0;
	
	storage = ext_variables_interpreter_get_storage(renv->interp);
	if ( storage == NULL ) return FALSE;
		
	if (sieve_binary_read_integer(renv->sbin, address, &index) ) {
		/* Parameter str can be NULL if we are requested to only skip and not 
		 * actually read the argument.
	 	*/
		if ( str != NULL ) {
			sieve_variable_get(storage, index, str);
		
			if ( *str == NULL ) *str = t_str_new(0);
		}
		return TRUE;
	}
	
	return FALSE;
}

bool ext_variables_opr_variable_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address, 
		struct sieve_variable_storage **storage, unsigned int *var_index)
{
	const struct sieve_operand *operand = sieve_operand_read(renv->sbin, address);
	sieve_size_t idx = 0;
	
	if ( operand != &variable_operand ) 
		return FALSE;
		
	*storage = ext_variables_interpreter_get_storage(renv->interp);
	if ( *storage == NULL ) return FALSE;
	
	if (sieve_binary_read_integer(renv->sbin, address, &idx) ) {
		*var_index = idx;
		return TRUE;
	}
	
	return FALSE;
}

/* Match value operand */

static bool opr_match_value_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str);
static bool opr_match_value_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);

const struct sieve_opr_string_interface match_value_interface = { 
	opr_match_value_dump,
	opr_match_value_read
};
		
const struct sieve_operand match_value_operand = { 
	"match-value", 
	&variables_extension, 
	EXT_VARIABLES_OPERAND_MATCH_VALUE,
	&string_class,
	&match_value_interface
};	

void ext_variables_opr_match_value_emit
	(struct sieve_binary *sbin, unsigned int index) 
{
	(void) sieve_operand_emit_code
		(sbin, &match_value_operand, ext_variables_my_id);
	(void) sieve_binary_emit_integer(sbin, index);
}

static bool opr_match_value_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address) 
{
	sieve_size_t index = 0;
	
	if (sieve_binary_read_integer(denv->sbin, address, &index) ) {
		sieve_code_dumpf(denv, "MVALUE: %ld", (long) index);

		return TRUE;
	}
	
	return FALSE;
}

static bool opr_match_value_read
  (const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str)
{ 
	sieve_size_t index = 0;
			
	if (sieve_binary_read_integer(renv->sbin, address, &index) ) {
		/* Parameter str can be NULL if we are requested to only skip and not 
		 * actually read the argument.
		 	*/
		if ( str != NULL ) {
			sieve_match_values_get(renv->interp, (unsigned int) index, str);
		
			if ( *str == NULL ) *str = t_str_new(0);
		}
		return TRUE;
	}
	
	return FALSE;
}

/* Variable string operand */

static bool opr_variable_string_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str);
static bool opr_variable_string_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);

const struct sieve_opr_string_interface variable_string_interface = { 
	opr_variable_string_dump,
	opr_variable_string_read
};
		
const struct sieve_operand variable_string_operand = { 
	"variable-string", 
	&variables_extension, 
	EXT_VARIABLES_OPERAND_VARIABLE_STRING,
	&string_class,
	&variable_string_interface
};	

void ext_variables_opr_variable_string_emit
	(struct sieve_binary *sbin, unsigned int elements) 
{
	(void) sieve_operand_emit_code
		(sbin, &variable_string_operand, ext_variables_my_id);
	(void) sieve_binary_emit_integer(sbin, elements);
}

static bool opr_variable_string_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address) 
{
	sieve_size_t elements = 0;
	unsigned int i;
	
	if (!sieve_binary_read_integer(denv->sbin, address, &elements) )
		return FALSE;
	
	sieve_code_dumpf(denv, "VARSTR [%ld]:", (long) elements);

	sieve_code_descend(denv);
	for ( i = 0; i < (unsigned int) elements; i++ ) {
		sieve_opr_string_dump(denv, address);
	}
	sieve_code_ascend(denv);
	
	return TRUE;
}

static bool opr_variable_string_read
  (const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str)
{ 
	sieve_size_t elements = 0;
	unsigned int i;
		
	if ( !sieve_binary_read_integer(renv->sbin, address, &elements) )
		return FALSE;

	/* Parameter str can be NULL if we are requested to only skip and not 
	 * actually read the argument.
	 */
	if ( str == NULL ) {
		for ( i = 0; i < (unsigned int) elements; i++ ) {		
			if ( !sieve_opr_string_read(renv, address, NULL) ) 
				return FALSE;
		}
	} else {
		*str = t_str_new(128);
		for ( i = 0; i < (unsigned int) elements; i++ ) {
			string_t *strelm;
		
			if ( !sieve_opr_string_read(renv, address, &strelm) ) 
				return FALSE;
		
			str_append_str(*str, strelm);
		}
	}

	return TRUE;
}

/* Set modifier registration */

const struct ext_variables_set_modifier *ext_variables_set_modifier_find
(struct sieve_validator *validator, const char *identifier)
{
	struct ext_variables_validator_context *ctx = 
		ext_variables_validator_context_get(validator);
		
	return (const struct ext_variables_set_modifier *) 
		hash_lookup(ctx->set_modifiers, identifier);
}

/* Interpreter context */


