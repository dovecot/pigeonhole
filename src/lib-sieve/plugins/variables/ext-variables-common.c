#include "lib.h"
#include "hash.h"
#include "str.h"

#include "sieve-common.h"
#include "sieve-ast.h"
#include "sieve-binary.h"
#include "sieve-code.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-code-dumper.h"
#include "sieve-interpreter.h"

#include "ext-variables-common.h"

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
		(pool, pool, 0, str_hash, (hash_cmp_callback_t *)strcmp);
		
	return scope;
}

struct sieve_variable *sieve_variable_scope_get_variable
(struct sieve_variable_scope *scope, const char *identifier)
{
	struct sieve_variable *var = 
		(struct sieve_variable *) hash_lookup(scope->variables, identifier);
	
	if ( var == NULL ) {
		var = p_new(scope->pool, struct sieve_variable, 1);
		var->identifier = identifier;
		var->index = scope->next_index++;
		
		hash_insert(scope->variables, (void *) identifier, (void *) var);
	} 
	
	return var;
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
(struct sieve_validator *validator, const char *variable)
{
	struct ext_variables_validator_context *ctx = 
		ext_variables_validator_context_get(validator);
		
	return sieve_variable_scope_get_variable(ctx->main_scope, variable);
}

/* Variable arguments */

static bool arg_variable_generate
	(struct sieve_generator *generator, struct sieve_ast_argument *arg, 
		struct sieve_command_context *context);

const struct sieve_argument variable_argument = 
	{ "@variable", NULL, NULL, NULL, arg_variable_generate };

void ext_variables_variable_argument_activate
(struct sieve_validator *validator, struct sieve_ast_argument *arg)
{
	struct ext_variables_validator_context *ctx;
	struct sieve_variable *var;
	
	ctx = ext_variables_validator_context_get(validator);
	var = sieve_variable_scope_get_variable(ctx->main_scope, 
		sieve_ast_argument_strc(arg));

	arg->argument = &variable_argument;
	arg->context = (void *) var;
}

static bool arg_variable_generate
(struct sieve_generator *generator, struct sieve_ast_argument *arg, 
	struct sieve_command_context *context ATTR_UNUSED)
{
	struct sieve_variable *var = (struct sieve_variable *) arg->context;
	
	ext_variables_variable_emit(sieve_generator_get_binary(generator), var);

	return TRUE;
}

/* Variable operands */

static bool opr_variable_read
	(struct sieve_binary *sbin, sieve_size_t *address, string_t **str);
static bool opr_variable_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);

const struct sieve_opr_string_interface variable_interface = { 
	opr_variable_dump,
	opr_variable_read
};
		
const struct sieve_operand variable_operand = { 
	"variable", 
	&variables_extension, 0,
	&string_class,
	&variable_interface
};	

void ext_variables_variable_emit
	(struct sieve_binary *sbin, struct sieve_variable *var) 
{
	(void) sieve_operand_emit_code(sbin, &variable_operand, ext_variables_my_id);
	(void) sieve_binary_emit_integer(sbin, var->index);
}

static bool opr_variable_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address) 
{
	sieve_size_t number = 0;
	
	if (sieve_binary_read_integer(denv->sbin, address, &number) ) {
		sieve_code_dumpf(denv, "VARIABLE: %ld [?]", (long) number);

		return TRUE;
	}
	
	return FALSE;
}

static bool opr_variable_read
  (struct sieve_binary *sbin, sieve_size_t *address, string_t **str)
{ 
	sieve_size_t number = 0;
	
	if (sieve_binary_read_integer(sbin, address, &number) ) {
		*str = t_str_new(10);
		str_append(*str, "VARIABLE");

		return TRUE;
	}
	
	return FALSE;
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


