/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "hash.h"
#include "str.h"
#include "array.h"

#include "sieve-common.h"
#include "sieve-settings.old.h"

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
#include "ext-variables-limits.h"
#include "ext-variables-name.h"
#include "ext-variables-modifiers.h"

/*
 * Limits
 */

unsigned int
sieve_variables_get_max_scope_size(const struct sieve_extension *var_ext)
{
	const struct ext_variables_context *extctx =
		ext_variables_get_context(var_ext);

	return extctx->max_scope_size;
}

size_t sieve_variables_get_max_variable_size(
	const struct sieve_extension *var_ext)
{
	const struct ext_variables_context *extctx =
		ext_variables_get_context(var_ext);

	return extctx->max_variable_size;
}

/*
 * Extension configuration
 */

int ext_variables_load(const struct sieve_extension *ext, void **context_r)
{
	struct sieve_instance *svinst = ext->svinst;
	struct ext_variables_context *extctx;
	unsigned long long int uint_setting;
	size_t size_setting;

	extctx = i_new(struct ext_variables_context, 1);

	/* Get limits */
	extctx->max_scope_size = EXT_VARIABLES_DEFAULT_MAX_SCOPE_SIZE;
	extctx->max_variable_size = EXT_VARIABLES_DEFAULT_MAX_VARIABLE_SIZE;

	if (sieve_setting_get_uint_value(
		svinst, "sieve_variables_max_scope_size", &uint_setting)) {
		if (uint_setting < EXT_VARIABLES_REQUIRED_MAX_SCOPE_SIZE) {
			e_warning(svinst->event, "variables: "
				  "setting sieve_variables_max_scope_size "
				  "is lower than required by standards "
				  "(>= %llu items)",
				  (unsigned long long)
					EXT_VARIABLES_REQUIRED_MAX_SCOPE_SIZE);
		} else {
			extctx->max_scope_size = (unsigned int)uint_setting;
		}
	}

	if (sieve_setting_get_size_value(
		svinst, "sieve_variables_max_variable_size", &size_setting)) {
		if (size_setting < EXT_VARIABLES_REQUIRED_MAX_VARIABLE_SIZE) {
			e_warning(svinst->event, "variables: "
				  "setting sieve_variables_max_variable_size "
				  "is lower than required by standards "
				  "(>= %zu bytes)",
				  (size_t)EXT_VARIABLES_REQUIRED_MAX_VARIABLE_SIZE);
		} else {
			extctx->max_variable_size = size_setting;
		}
	}

	*context_r = extctx;
	return 0;
}

void ext_variables_unload(const struct sieve_extension *ext)
{
	struct ext_variables_context *extctx = ext->context;

	i_free(extctx);
}

const struct ext_variables_context *
ext_variables_get_context(const struct sieve_extension *var_ext)
{
	const struct ext_variables_context *extctx = var_ext->context;

	i_assert(var_ext->def == &variables_extension);
	return extctx;
}

/*
 * Variable scope
 */

struct sieve_variable_scope {
	pool_t pool;
	int refcount;

	struct sieve_instance *svinst;
	const struct sieve_extension *var_ext;
	const struct sieve_extension *ext;

	struct sieve_variable *error_var;

	HASH_TABLE(const char *, struct sieve_variable *) variables;
	ARRAY(struct sieve_variable *) variable_index;
};

struct sieve_variable_scope_binary {
	struct sieve_variable_scope *scope;

	unsigned int size;
	struct sieve_binary_block *sblock;
	sieve_size_t address;
};

struct sieve_variable_scope_iter {
	struct sieve_variable_scope *scope;
	struct hash_iterate_context *hctx;
};

struct sieve_variable_scope *
sieve_variable_scope_create(struct sieve_instance *svinst,
			    const struct sieve_extension *var_ext,
			    const struct sieve_extension *ext)
{
	struct sieve_variable_scope *scope;
	pool_t pool;

	i_assert(var_ext->def == &variables_extension);

	pool = pool_alloconly_create("sieve_variable_scope", 4096);
	scope = p_new(pool, struct sieve_variable_scope, 1);
	scope->pool = pool;
	scope->refcount = 1;

	scope->svinst = svinst;
	scope->var_ext = var_ext;
	scope->ext = ext;

	hash_table_create(&scope->variables, pool, 0, strcase_hash, strcasecmp);
	p_array_init(&scope->variable_index, pool, 128);

	return scope;
}

void sieve_variable_scope_ref(struct sieve_variable_scope *scope)
{
	scope->refcount++;
}

void sieve_variable_scope_unref(struct sieve_variable_scope **_scope)
{
	struct sieve_variable_scope *scope = *_scope;

	i_assert(scope->refcount > 0);

	if (--scope->refcount != 0)
		return;

	hash_table_destroy(&scope->variables);

	*_scope = NULL;
	pool_unref(&scope->pool);
}

pool_t sieve_variable_scope_pool(struct sieve_variable_scope *scope)
{
	return scope->pool;
}

struct sieve_variable *
sieve_variable_scope_declare(struct sieve_variable_scope *scope,
			     const char *identifier)
{
	unsigned int max_scope_size;
	struct sieve_variable *var;

	var = hash_table_lookup(scope->variables, identifier);
	if (var != NULL)
		return var;

	max_scope_size = sieve_variables_get_max_scope_size(scope->var_ext);
	if (array_count(&scope->variable_index) >= max_scope_size) {
		if (scope->error_var == NULL) {
			var = p_new(scope->pool, struct sieve_variable, 1);
			var->identifier = "@ERROR@";
			var->index = 0;

			scope->error_var = var;
			return NULL;
		}

		return scope->error_var;
	}

	var = p_new(scope->pool, struct sieve_variable, 1);
	var->ext = scope->ext;
	var->identifier = p_strdup(scope->pool, identifier);
	var->index = array_count(&scope->variable_index);

	hash_table_insert(scope->variables, var->identifier, var);
	array_append(&scope->variable_index, &var, 1);
	return var;
}

struct sieve_variable *
sieve_variable_scope_get_variable(struct sieve_variable_scope *scope,
				  const char *identifier)
{
	return hash_table_lookup(scope->variables, identifier);
}

struct sieve_variable *
sieve_variable_scope_import(struct sieve_variable_scope *scope,
			    struct sieve_variable *var)
{
	struct sieve_variable *old_var, *new_var;

	old_var = sieve_variable_scope_get_variable(scope, var->identifier);
	if (old_var != NULL) {
		i_assert(memcmp(old_var, var, sizeof(*var)) == 0);
		return old_var;
	}

	new_var = p_new(scope->pool, struct sieve_variable, 1);
	memcpy(new_var, var, sizeof(*new_var));

	hash_table_insert(scope->variables, new_var->identifier, new_var);

	/* Not entered into the index because it is an external variable
	   (This can be done unlimited; only limited by the size of the external
	   scope)
	 */
	return new_var;
}

struct sieve_variable_scope_iter *
sieve_variable_scope_iterate_init(struct sieve_variable_scope *scope)
{
	struct sieve_variable_scope_iter *iter;

	iter = t_new(struct sieve_variable_scope_iter, 1);
	iter->scope = scope;
	iter->hctx = hash_table_iterate_init(scope->variables);

	return iter;
}

bool sieve_variable_scope_iterate(struct sieve_variable_scope_iter *iter,
				  struct sieve_variable **var_r)
{
	const char *key;

	return hash_table_iterate(iter->hctx, iter->scope->variables,
				  &key, var_r);
}

void sieve_variable_scope_iterate_deinit(
	struct sieve_variable_scope_iter **iter)
{
	hash_table_iterate_deinit(&(*iter)->hctx);
	*iter = NULL;
}

unsigned int
sieve_variable_scope_declarations(struct sieve_variable_scope *scope)
{
	return hash_table_count(scope->variables);
}

unsigned int sieve_variable_scope_size(struct sieve_variable_scope *scope)
{
	return array_count(&scope->variable_index);
}

struct sieve_variable *const *
sieve_variable_scope_get_variables(struct sieve_variable_scope *scope,
				   unsigned int *size_r)
{
	return array_get(&scope->variable_index, size_r);
}

struct sieve_variable *
sieve_variable_scope_get_indexed(struct sieve_variable_scope *scope,
				 unsigned int index)
{
	struct sieve_variable *const *var;

	if (index >= array_count(&scope->variable_index))
		return NULL;

	var = array_idx(&scope->variable_index, index);
	return *var;
}

/* Scope binary */

struct sieve_variable_scope *
sieve_variable_scope_binary_dump(struct sieve_instance *svinst,
				 const struct sieve_extension *var_ext,
				 const struct sieve_extension *ext,
				 const struct sieve_dumptime_env *denv,
				 sieve_size_t *address)
{
	struct sieve_variable_scope *local_scope;
	unsigned int i, scope_size;
	sieve_size_t pc;
	sieve_offset_t end_offset;

	/* Read scope size */
	sieve_code_mark(denv);
	if (!sieve_binary_read_unsigned(denv->sblock, address, &scope_size))
		return NULL;

	/* Read offset */
	pc = *address;
	if (!sieve_binary_read_offset(denv->sblock, address, &end_offset))
		return NULL;

	/* Create scope */
	local_scope = sieve_variable_scope_create(svinst, var_ext, ext);

	/* Read and dump scope itself */

	sieve_code_dumpf(denv, "VARIABLES SCOPE [%u] (end: %08x)",
			 scope_size, (unsigned int)(pc + end_offset));

	for (i = 0; i < scope_size; i++) {
		string_t *identifier;

		sieve_code_mark(denv);
		if (!sieve_binary_read_string(denv->sblock, address,
					      &identifier))
			return NULL;

		sieve_code_dumpf(denv, "%3d: '%s'", i, str_c(identifier));

		(void)sieve_variable_scope_declare(local_scope,
						   str_c(identifier));
	}

	return local_scope;
}

struct sieve_variable_scope_binary *
sieve_variable_scope_binary_create(struct sieve_variable_scope *scope)
{
	struct sieve_variable_scope_binary *scpbin;

	scpbin = p_new(scope->pool, struct sieve_variable_scope_binary, 1);
	scpbin->scope = scope;

	return scpbin;
}

void sieve_variable_scope_binary_ref(struct sieve_variable_scope_binary *scpbin)
{
	sieve_variable_scope_ref(scpbin->scope);
}

void sieve_variable_scope_binary_unref(
	struct sieve_variable_scope_binary **scpbin)
{
	sieve_variable_scope_unref(&(*scpbin)->scope);
	*scpbin = NULL;
}

struct sieve_variable_scope_binary *
sieve_variable_scope_binary_read(struct sieve_instance *svinst,
				 const struct sieve_extension *var_ext,
				 const struct sieve_extension *ext,
				 struct sieve_binary_block *sblock,
				 sieve_size_t *address)
{
	struct sieve_variable_scope *scope;
	struct sieve_variable_scope_binary *scpbin;
	unsigned int scope_size, max_scope_size;
	const char *ext_name = (ext == NULL ? "variables" :
				sieve_extension_name(ext));
	sieve_size_t pc;
	sieve_offset_t end_offset;

	/* Read scope size */
	if (!sieve_binary_read_unsigned(sblock, address, &scope_size)) {
		e_error(svinst->event, "%s: "
			"variable scope: failed to read size", ext_name);
		return NULL;
	}

	/* Check size limit */
	max_scope_size = sieve_variables_get_max_scope_size(var_ext);
	if (scope_size > max_scope_size) {
		e_error(svinst->event, "%s: "
			"variable scope: size exceeds the limit (%u > %u)",
			ext_name, scope_size, max_scope_size);
		return NULL;
	}

	/* Read offset */
	pc = *address;
	if (!sieve_binary_read_offset(sblock, address, &end_offset)) {
		e_error(svinst->event, "%s: "
			"variable scope: failed to read end offset", ext_name);
		return NULL;
	}

	/* Create scope */
	scope = sieve_variable_scope_create(svinst, var_ext, ext);

	scpbin = sieve_variable_scope_binary_create(scope);
	scpbin->size = scope_size;
	scpbin->sblock = sblock;
	scpbin->address = *address;

	*address = pc + end_offset;

	return scpbin;
}

struct sieve_variable_scope *
sieve_variable_scope_binary_get(struct sieve_variable_scope_binary *scpbin)
{
	const struct sieve_extension *ext = scpbin->scope->ext;
	struct sieve_instance *svinst = scpbin->scope->svinst;
	const char *ext_name = (ext == NULL ? "variables" :
				sieve_extension_name(ext));
	unsigned int i;

	if (scpbin->sblock != NULL) {
		sieve_size_t *address = &scpbin->address;

		/* Read scope itself */
		for (i = 0; i < scpbin->size; i++) {
			struct sieve_variable *var;
			string_t *identifier;

			if (!sieve_binary_read_string(scpbin->sblock, address,
						      &identifier)) {
				e_error(svinst->event, "%s: variable scope: "
					"failed to read variable name",
					ext_name);
				return NULL;
			}

			var = sieve_variable_scope_declare(scpbin->scope,
							   str_c(identifier));

			i_assert(var != NULL);
			i_assert(var->index == i);
		}

		scpbin->sblock = NULL;
	}

	return scpbin->scope;
}

unsigned int
sieve_variable_scope_binary_get_size(
	struct sieve_variable_scope_binary *scpbin)
{
	if (scpbin->sblock != NULL)
		return scpbin->size;

	return array_count(&scpbin->scope->variable_index);
}

/*
 * Variable storage
 */

struct sieve_variable_storage {
	pool_t pool;
	const struct sieve_extension *var_ext;
	struct sieve_variable_scope *scope;
	struct sieve_variable_scope_binary *scope_bin;
	unsigned int max_size;
	ARRAY(string_t *) var_values;
};

struct sieve_variable_storage *
sieve_variable_storage_create(const struct sieve_extension *var_ext,
			      pool_t pool,
			      struct sieve_variable_scope_binary *scpbin)
{
	struct sieve_variable_storage *storage;

	storage = p_new(pool, struct sieve_variable_storage, 1);
	storage->pool = pool;
	storage->var_ext = var_ext;
	storage->scope_bin = scpbin;
	storage->scope = NULL;

	storage->max_size = sieve_variable_scope_binary_get_size(scpbin);

	p_array_init(&storage->var_values, pool, 4);

	return storage;
}

static inline bool
sieve_variable_valid(struct sieve_variable_storage *storage,
		     unsigned int index)
{
	if (storage->scope_bin == NULL)
		return TRUE;

	return (index < storage->max_size);
}

bool sieve_variable_get_identifier(struct sieve_variable_storage *storage,
				   unsigned int index, const char **identifier)
{
	struct sieve_variable *const *var;

	*identifier = NULL;

	if (storage->scope_bin == NULL)
		return TRUE;

	if (storage->scope == NULL) {
		storage->scope =
			sieve_variable_scope_binary_get(storage->scope_bin);
		if (storage->scope == NULL)
			return FALSE;
	}

	/* FIXME: direct invasion of the scope object is a bit ugly */
	if (index >= array_count(&storage->scope->variable_index))
		return FALSE;

	var = array_idx(&storage->scope->variable_index, index);
	if (*var != NULL)
		*identifier = (*var)->identifier;
	return TRUE;
}

const char *
sieve_variable_get_varid(struct sieve_variable_storage *storage,
			 unsigned int index)
{
	if (storage->scope_bin == NULL)
		return t_strdup_printf("%ld", (long)index);

	if (storage->scope == NULL) {
		storage->scope =
			sieve_variable_scope_binary_get(storage->scope_bin);
		if (storage->scope == NULL)
			return NULL;
	}

	return sieve_ext_variables_get_varid(storage->scope->ext, index);
}

bool sieve_variable_get(struct sieve_variable_storage *storage,
			unsigned int index, string_t **value)
{
	*value = NULL;

	if (index < array_count(&storage->var_values)) {
		string_t *const *varent;

		varent = array_idx(&storage->var_values, index);

		*value = *varent;
	} else if (!sieve_variable_valid(storage, index)) {
		return FALSE;
	}

	return TRUE;
}

bool sieve_variable_get_modifiable(struct sieve_variable_storage *storage,
				   unsigned int index, string_t **value)
{
	string_t *dummy;

	if (value == NULL)
		value = &dummy;

	if (!sieve_variable_get(storage, index, value))
		return FALSE;

	if (*value == NULL) {
		*value = str_new(storage->pool, 256);
		array_idx_set(&storage->var_values, index, value);
	}
	return TRUE;
}

bool sieve_variable_assign(struct sieve_variable_storage *storage,
			   unsigned int index, const string_t *value)
{
	const struct ext_variables_context *extctx =
		ext_variables_get_context(storage->var_ext);
	string_t *varval;

	if (!sieve_variable_get_modifiable(storage, index, &varval))
		return FALSE;

	str_truncate(varval, 0);
	str_append_str(varval, value);

	/* Just a precaution, caller should prevent this in the first place */
	if (str_len(varval) > extctx->max_variable_size)
		str_truncate_utf8(varval, extctx->max_variable_size);

	return TRUE;
}

bool sieve_variable_assign_cstr(struct sieve_variable_storage *storage,
				unsigned int index, const char *value)
{
	const struct ext_variables_context *extctx =
		ext_variables_get_context(storage->var_ext);
	string_t *varval;

	if (!sieve_variable_get_modifiable(storage, index, &varval))
		return FALSE;

	str_truncate(varval, 0);
	str_append(varval, value);

	/* Just a precaution, caller should prevent this in the first place */
	if (str_len(varval) > extctx->max_variable_size)
		str_truncate_utf8(varval, extctx->max_variable_size);

	return TRUE;
}

/*
 * AST Context
 */

static void
ext_variables_ast_free(const struct sieve_extension *ext ATTR_UNUSED,
		       struct sieve_ast *ast ATTR_UNUSED, void *context)
{
	struct sieve_variable_scope *local_scope =
		(struct sieve_variable_scope *)context;

	/* Unreference main variable scope */
	sieve_variable_scope_unref(&local_scope);
}

static const struct sieve_ast_extension variables_ast_extension = {
	&variables_extension,
	ext_variables_ast_free,
};

static struct sieve_variable_scope *
ext_variables_create_local_scope(const struct sieve_extension *this_ext,
				 struct sieve_ast *ast)
{
	struct sieve_variable_scope *scope;

	scope = sieve_variable_scope_create(this_ext->svinst, this_ext, NULL);

	sieve_ast_extension_register(ast, this_ext, &variables_ast_extension,
				     scope);
	return scope;
}

static struct sieve_variable_scope *
ext_variables_ast_get_local_scope(const struct sieve_extension *this_ext,
				  struct sieve_ast *ast)
{
	struct sieve_variable_scope *local_scope =
		(struct sieve_variable_scope *)
			sieve_ast_extension_get_context(ast, this_ext);

	return local_scope;
}

/*
 * Validator context
 */

static struct ext_variables_validator_context *
ext_variables_validator_context_create(const struct sieve_extension *this_ext,
				       struct sieve_validator *valdtr)
{
	pool_t pool = sieve_validator_pool(valdtr);
	struct ext_variables_validator_context *ctx;
	struct sieve_ast *ast = sieve_validator_ast(valdtr);

	ctx = p_new(pool, struct ext_variables_validator_context, 1);
	ctx->modifiers = sieve_validator_object_registry_create(valdtr);
	ctx->namespaces = sieve_validator_object_registry_create(valdtr);
	ctx->local_scope = ext_variables_create_local_scope(this_ext, ast);

	sieve_validator_extension_set_context(valdtr, this_ext, ctx);
	return ctx;
}

struct ext_variables_validator_context *
ext_variables_validator_context_get(const struct sieve_extension *this_ext,
				    struct sieve_validator *valdtr)
{
	struct ext_variables_validator_context *ctx;

	i_assert(sieve_extension_is(this_ext, variables_extension));
	ctx = (struct ext_variables_validator_context *)
		sieve_validator_extension_get_context(valdtr, this_ext);

	if (ctx == NULL)
		ctx = ext_variables_validator_context_create(this_ext, valdtr);
	return ctx;
}

void ext_variables_validator_initialize(const struct sieve_extension *this_ext,
					struct sieve_validator *valdtr)
{
	struct ext_variables_validator_context *ctx;

	/* Create our context */
	ctx = ext_variables_validator_context_get(this_ext, valdtr);

	ext_variables_register_core_modifiers(this_ext, ctx);

	ctx->active = TRUE;
}

struct sieve_variable *ext_variables_validator_get_variable(
	const struct sieve_extension *this_ext,
	struct sieve_validator *validator, const char *variable)
{
	struct ext_variables_validator_context *ctx =
		ext_variables_validator_context_get(this_ext, validator);

	return sieve_variable_scope_get_variable(ctx->local_scope, variable);
}

struct sieve_variable *
ext_variables_validator_declare_variable(const struct sieve_extension *this_ext,
					 struct sieve_validator *validator,
					 const char *variable)
{
	struct ext_variables_validator_context *ctx =
		ext_variables_validator_context_get(this_ext, validator);

	return sieve_variable_scope_declare(ctx->local_scope, variable);
}

struct sieve_variable_scope *
sieve_ext_variables_get_local_scope(const struct sieve_extension *var_ext,
				    struct sieve_validator *validator)
{
	struct ext_variables_validator_context *ctx =
		ext_variables_validator_context_get(var_ext, validator);

	return ctx->local_scope;
}

bool sieve_ext_variables_is_active(const struct sieve_extension *var_ext,
				   struct sieve_validator *valdtr)
{
	struct ext_variables_validator_context *ctx =
		ext_variables_validator_context_get(var_ext, valdtr);

	return (ctx != NULL && ctx->active);
}

/*
 * Code generation
 */

bool ext_variables_generator_load(const struct sieve_extension *ext,
				  const struct sieve_codegen_env *cgenv)
{
	struct sieve_variable_scope *local_scope =
		ext_variables_ast_get_local_scope(ext, cgenv->ast);
	unsigned int count = sieve_variable_scope_size(local_scope);
	sieve_size_t jump;

	sieve_binary_emit_unsigned(cgenv->sblock, count);

	jump = sieve_binary_emit_offset(cgenv->sblock, 0);

	if (count > 0) {
		unsigned int size, i;
		struct sieve_variable *const *vars =
			sieve_variable_scope_get_variables(local_scope, &size);

		for (i = 0; i < size; i++) {
			sieve_binary_emit_cstring(cgenv->sblock,
						  vars[i]->identifier);
		}
	}

	sieve_binary_resolve_offset(cgenv->sblock, jump);
	return TRUE;
}

/*
 * Interpreter context
 */

struct ext_variables_interpreter_context {
	pool_t pool;

	struct sieve_variable_scope *local_scope;
	struct sieve_variable_scope_binary *local_scope_bin;

	struct sieve_variable_storage *local_storage;
	ARRAY(struct sieve_variable_storage *) ext_storages;
};

static void
ext_variables_interpreter_free(const struct sieve_extension *ext ATTR_UNUSED,
			       struct sieve_interpreter *interp ATTR_UNUSED,
			       void *context)
{
	struct ext_variables_interpreter_context *ctx =
		(struct ext_variables_interpreter_context *)context;

	sieve_variable_scope_binary_unref(&ctx->local_scope_bin);
}

static struct sieve_interpreter_extension
variables_interpreter_extension = {
	.ext_def = &variables_extension,
	.free = ext_variables_interpreter_free,
};

static struct ext_variables_interpreter_context *
ext_variables_interpreter_context_create(
	const struct sieve_extension *this_ext,
	struct sieve_interpreter *interp,
	struct sieve_variable_scope_binary *scpbin)
{
	pool_t pool = sieve_interpreter_pool(interp);
	struct ext_variables_interpreter_context *ctx;

	ctx = p_new(pool, struct ext_variables_interpreter_context, 1);
	ctx->pool = pool;
	ctx->local_scope = NULL;
	ctx->local_scope_bin = scpbin;
	ctx->local_storage =
		sieve_variable_storage_create(this_ext, pool, scpbin);
	p_array_init(&ctx->ext_storages, pool,
		     sieve_extensions_get_count(this_ext->svinst));

	sieve_interpreter_extension_register(interp, this_ext,
					     &variables_interpreter_extension,
					     ctx);
	return ctx;
}

bool ext_variables_interpreter_load(const struct sieve_extension *ext,
				    const struct sieve_runtime_env *renv,
				    sieve_size_t *address)
{
	const struct sieve_execute_env *eenv = renv->exec_env;
	struct sieve_variable_scope_binary *scpbin;

	scpbin = sieve_variable_scope_binary_read(eenv->svinst, ext, NULL,
						  renv->sblock, address);
	if (scpbin == NULL)
		return FALSE;

	/* Create our context */
	(void)ext_variables_interpreter_context_create(ext, renv->interp,
						       scpbin);

	/* Enable support for match values */
	(void)sieve_match_values_set_enabled(renv, TRUE);

	return TRUE;
}

static inline struct ext_variables_interpreter_context *
ext_variables_interpreter_context_get(const struct sieve_extension *this_ext,
				      struct sieve_interpreter *interp)
{
	struct ext_variables_interpreter_context *ctx;

	i_assert(sieve_extension_is(this_ext, variables_extension));
	ctx = (struct ext_variables_interpreter_context *)
		sieve_interpreter_extension_get_context(interp, this_ext);
	return ctx;
}

struct sieve_variable_storage *
sieve_ext_variables_runtime_get_storage(const struct sieve_extension *var_ext,
					const struct sieve_runtime_env *renv,
					const struct sieve_extension *ext)
{
	struct ext_variables_interpreter_context *ctx =
		ext_variables_interpreter_context_get(var_ext, renv->interp);
	struct sieve_variable_storage *const *storage;

	if (ext == NULL)
		return ctx->local_storage;

	if (ext->id >= (int)array_count(&ctx->ext_storages))
		storage = NULL;
	else
		storage = array_idx(&ctx->ext_storages, ext->id);

	if (storage == NULL)
		return NULL;
	return *storage;
}

void sieve_ext_variables_runtime_set_storage(
	const struct sieve_extension *var_ext,
	const struct sieve_runtime_env *renv, const struct sieve_extension *ext,
	struct sieve_variable_storage *storage)
{
	struct ext_variables_interpreter_context *ctx =
		ext_variables_interpreter_context_get(var_ext, renv->interp);

	if (ctx == NULL || ext == NULL || storage == NULL)
		return;
	if (ext->id < 0)
		return;

	array_idx_set(&ctx->ext_storages, (unsigned int) ext->id, &storage);
}
