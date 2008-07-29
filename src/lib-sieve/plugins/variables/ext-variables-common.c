#include "lib.h"
#include "hash.h"
#include "str.h"
#include "array.h"

#include "sieve-common.h"

#include "sieve-ast.h"
#include "sieve-binary.h"
#include "sieve-code.h"
#include "sieve-objects.h"
#include "sieve-match-types.h"

#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-dump.h"
#include "sieve-interpreter.h"

#include "ext-variables-common.h"
#include "ext-variables-name.h"
#include "ext-variables-modifiers.h"

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
	
/* Variable scope */

struct sieve_variable_scope {
	pool_t pool;
	int refcount;

	const struct sieve_extension *ext;

	unsigned int next_index;
	struct hash_table *variables;
};

struct sieve_variable_scope *sieve_variable_scope_create
	(pool_t pool, const struct sieve_extension *ext) 
{
	struct sieve_variable_scope *scope;
	
	scope = p_new(pool, struct sieve_variable_scope, 1);
	scope->pool = pool;
	scope->refcount = 1;

	scope->ext = ext;
	scope->variables = hash_create
		(default_pool, pool, 0, strcase_hash, (hash_cmp_callback_t *)strcasecmp);
		
	return scope;
}

void sieve_variable_scope_ref(struct sieve_variable_scope *scope)
{
    scope->refcount++;
}

void sieve_variable_scope_unref(struct sieve_variable_scope **scope)
{
    i_assert((*scope)->refcount > 0);

    if (--(*scope)->refcount != 0)
        return;

	hash_destroy(&(*scope)->variables);
    *scope = NULL;
}

struct sieve_variable *sieve_variable_scope_declare
(struct sieve_variable_scope *scope, const char *identifier)
{
	struct sieve_variable *new_var = p_new(scope->pool, struct sieve_variable, 1);
	new_var->identifier = p_strdup(scope->pool, identifier);
	new_var->index = scope->next_index++;
	new_var->ext = scope->ext;

	hash_insert(scope->variables, (void *) new_var->identifier, (void *) new_var);
	
	return new_var;
}

struct sieve_variable *sieve_variable_scope_get_variable
(struct sieve_variable_scope *scope, const char *identifier, bool declare)
{
	struct sieve_variable *var = 
		(struct sieve_variable *) hash_lookup(scope->variables, identifier);

	
	if ( var == NULL && declare ) {
		var = sieve_variable_scope_declare(scope, identifier);
	}

	return var;
}

struct sieve_variable *sieve_variable_scope_import
(struct sieve_variable_scope *scope, struct sieve_variable *var)
{
	struct sieve_variable *new_var = p_new(scope->pool, struct sieve_variable, 1);
	memcpy(new_var, var, sizeof(struct sieve_variable));
		
	hash_insert(scope->variables, (void *) new_var->identifier, (void *) new_var);
	
	return new_var;
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

void sieve_variable_get_modifiable
(struct sieve_variable_storage *storage, unsigned int index, string_t **value)
{
	sieve_variable_get(storage, index, value);
	
	if ( *value == NULL ) {
		*value = str_new(storage->pool, 256);
		array_idx_set(&storage->var_values, index, value);	
	} 
}

void sieve_variable_assign
(struct sieve_variable_storage *storage, unsigned int index, 
	const string_t *value)
{
	string_t *varval;
	
	sieve_variable_get_modifiable(storage, index, &varval);

	if ( varval == NULL ) {
		varval = str_new(storage->pool, str_len(value));
		array_idx_set(&storage->var_values, index, &varval);	
	} 

	str_truncate(varval, 0);
	str_append_str(varval, value);
}

/*
 * AST Context
 */

static void ext_variables_ast_free
(struct sieve_ast *ast ATTR_UNUSED, void *context)
{
	struct sieve_variable_scope *main_scope =
		(struct sieve_variable_scope *) context;

    /* Unreference main variable scope */
    sieve_variable_scope_unref(&main_scope);
}

static const struct sieve_ast_extension variables_ast_extension = {
    &variables_extension,
    ext_variables_ast_free
};

static struct sieve_variable_scope *ext_variables_create_main_scope
(struct sieve_ast *ast)
{
    struct sieve_variable_scope *scope;
    pool_t pool = sieve_ast_pool(ast);

	scope = sieve_variable_scope_create(pool, NULL);

    sieve_ast_extension_register(ast, &variables_ast_extension, (void *) scope);

    return scope;
}

/*
 * Validator context 
 */

static struct ext_variables_validator_context *
ext_variables_validator_context_create(struct sieve_validator *valdtr)
{		
	pool_t pool = sieve_validator_pool(valdtr);
	struct ext_variables_validator_context *ctx;
	struct sieve_ast *ast = sieve_validator_ast(valdtr);
	
	ctx = p_new(pool, struct ext_variables_validator_context, 1);
	ctx->modifiers = sieve_validator_object_registry_create(valdtr);
	ctx->main_scope = ext_variables_create_main_scope(ast);

	sieve_validator_extension_set_context
		(valdtr, &variables_extension, (void *) ctx);

	return ctx;
}

void ext_variables_validator_initialize(struct sieve_validator *validator)
{
	struct ext_variables_validator_context *ctx;
	
	/* Create our context */
	ctx = ext_variables_validator_context_create(validator);
	
	ext_variables_register_core_modifiers(ctx);
}

struct sieve_variable *ext_variables_validator_get_variable
(struct sieve_validator *validator, const char *variable, bool declare)
{
	struct ext_variables_validator_context *ctx = 
		ext_variables_validator_context_get(validator);
		
	return sieve_variable_scope_get_variable(ctx->main_scope, variable, declare);
}

struct sieve_variable_scope *sieve_ext_variables_get_main_scope
(struct sieve_validator *validator)
{
	struct ext_variables_validator_context *ctx = 
		ext_variables_validator_context_get(validator);
		
	return ctx->main_scope;
}

bool sieve_ext_variables_is_active(struct sieve_validator *valdtr)
{
	return ( ext_variables_validator_context_get(valdtr) != NULL );
}

/* Interpreter context */

struct ext_variables_interpreter_context {
	struct sieve_variable_storage *local_storage;
	ARRAY_DEFINE(ext_storages, struct sieve_variable_storage *);
};

static struct ext_variables_interpreter_context *
ext_variables_interpreter_context_create(struct sieve_interpreter *interp)
{		
	pool_t pool = sieve_interpreter_pool(interp);
	struct ext_variables_interpreter_context *ctx;
	
	ctx = p_new(pool, struct ext_variables_interpreter_context, 1);
	ctx->local_storage = sieve_variable_storage_create(pool);
	p_array_init(&ctx->ext_storages, pool, sieve_extensions_get_count());

	sieve_interpreter_extension_set_context
		(interp, &variables_extension, (void *) ctx);

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
		sieve_interpreter_extension_get_context(interp, &variables_extension);
}

struct sieve_variable_storage *sieve_ext_variables_get_storage
	(struct sieve_interpreter *interp, const struct sieve_extension *ext)
{
	struct ext_variables_interpreter_context *ctx = 
		ext_variables_interpreter_context_get(interp);
	struct sieve_variable_storage * const *storage;
	int ext_id;
		
	if ( ext == NULL )
		return ctx->local_storage;

	ext_id = *ext->id;
	if ( ext_id >= (int) array_count(&ctx->ext_storages) ) {
		storage = NULL;
	} else {
		storage = array_idx(&ctx->ext_storages, ext_id);
	}
	
	if ( storage == NULL || *storage == NULL ) {
		pool_t pool = sieve_interpreter_pool(interp);
		struct sieve_variable_storage *strg = sieve_variable_storage_create(pool);
		
		array_idx_set(&ctx->ext_storages, (unsigned int) ext_id, &strg);
		
		return strg;		
	}
	
	return *storage;
}

void sieve_ext_variables_set_storage
(struct sieve_interpreter *interp, struct sieve_variable_storage *storage,
	const struct sieve_extension *ext)
{
	struct ext_variables_interpreter_context *ctx = 
		ext_variables_interpreter_context_get(interp);
		
	if ( ext == NULL || storage == NULL )
		return;
		
	array_idx_set(&ctx->ext_storages, (unsigned int) *ext->id, &storage);
}



