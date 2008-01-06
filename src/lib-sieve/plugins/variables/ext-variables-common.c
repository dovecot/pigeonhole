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
(struct sieve_validator *validator, const char *variable)
{
	struct ext_variables_validator_context *ctx = 
		ext_variables_validator_context_get(validator);
		
	return sieve_variable_scope_get_variable(ctx->main_scope, variable);
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
 * Arguments 
 */

/* Variable argument */

static bool arg_variable_generate
	(struct sieve_generator *generator, struct sieve_ast_argument *arg, 
		struct sieve_command_context *context);

const struct sieve_argument variable_argument = 
	{ "@variable", NULL, NULL, NULL, arg_variable_generate };

static struct sieve_ast_argument *ext_variables_variable_argument_create
(struct sieve_validator *validator, struct sieve_ast *ast, 
	unsigned int source_line,	const char *variable)
{
	struct ext_variables_validator_context *ctx;
	struct sieve_variable *var;
	struct sieve_ast_argument *arg;
	
	ctx = ext_variables_validator_context_get(validator);
	var = sieve_variable_scope_get_variable(ctx->main_scope, variable);

	if ( var == NULL ) 
		return NULL;
	
	arg = sieve_ast_argument_create(ast, source_line);
	arg->type = SAAT_STRING;
	arg->argument = &variable_argument;
	arg->context = (void *) var;
	
	return arg;
}

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
	
	ext_variables_opr_variable_emit(sieve_generator_get_binary(generator), var);

	return TRUE;
}

/* Variable string argument */

static bool arg_variable_string_validate
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
		struct sieve_command_context *context);
static bool arg_variable_string_generate
(struct sieve_generator *generator, struct sieve_ast_argument *arg, 
	struct sieve_command_context *context);

const struct sieve_argument variable_string_argument = { 
	"@variable-string", 
	NULL, 
	arg_variable_string_validate, 
	NULL, 
	arg_variable_string_generate,
};

struct _variable_string_data {
	struct sieve_ast_arg_list *str_parts;
};

inline static struct sieve_ast_argument *_add_string_element
(struct sieve_ast_arg_list *list, struct sieve_ast_argument *arg)
{
	struct sieve_ast_argument *strarg = 
		sieve_ast_argument_create(arg->ast, arg->source_line);
	sieve_ast_arg_list_add(list, strarg);
	strarg->type = SAAT_STRING;

	return strarg;
}

static bool arg_variable_string_validate
(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
		struct sieve_command_context *cmd)
{
	enum { ST_NONE, ST_OPEN, ST_VARIABLE, ST_CLOSE } state = ST_NONE;
	pool_t pool = sieve_ast_pool((*arg)->ast);
	struct sieve_ast_arg_list *arglist = NULL;
	string_t *str = sieve_ast_argument_str(*arg);
	string_t *ident;
	const char *p, *mark, *strstart, *substart = NULL;
	const char *strval = (const char *) str_data(str);
	const char *strend = strval + str_len(str);
	struct _variable_string_data *strdata;
	bool result = TRUE;
	
	//t_push();
			
	ident = t_str_new(32);	
		
	p = strval;
	strstart = p;
	while ( result && p < strend ) {
		switch ( state ) {
		case ST_NONE:
			if ( *p == '$' ) {
				substart = p;
				state = ST_OPEN;
			}
			p++;
			break;
		case ST_OPEN:
			if ( *p == '{' ) {
				state = ST_VARIABLE;
				p++;
			} else 
				state = ST_NONE;
			break;
		case ST_VARIABLE:
			mark = p;
			
			if ( p < strend ) {
				if (*p == '_' || isalpha(*p) ) {
					str_append_c(ident, *p);
					p++;
				
					while ( p < strend && (*p == '_' || isalnum(*p)) ) {
						str_append_c(ident, *p);
						p++;
					}
					state = ST_CLOSE;
				} else if ( isdigit(*p) ) {
					unsigned int num_variable = *p - '0';
					p++;
					
					while ( p < strend && isdigit(*p) ) {
						num_variable = num_variable*10 + (*p - '0');
						p++;
					} 
					state = ST_CLOSE;
				} else 
					state = ST_NONE;
			}  			
			
			break;
		case ST_CLOSE:
			if ( *p == '}' ) {				
				struct sieve_ast_argument *strarg;
				
				/* We now know that the substitution is valid */	
				
				if ( arglist == NULL ) {
					arglist = sieve_ast_arg_list_create(pool);
				}
				
				if ( substart > strstart ) {
					strarg = _add_string_element(arglist, *arg);
					strarg->_value.str = str_new(pool, substart - strstart);
					str_append_n(strarg->_value.str, strstart, substart - strstart); 
					
					if ( !sieve_validator_argument_activate_super
						(validator, cmd, strarg, FALSE) )
						return FALSE;
						
					printf("STR: %s\n", t_strdup_until(strstart, substart));
				}
				
				/* Find the variable */
				strarg = ext_variables_variable_argument_create
					(validator, (*arg)->ast, (*arg)->source_line, str_c(ident));
				if ( strarg != NULL )
					sieve_ast_arg_list_add(arglist, strarg);

				printf("VAR: %s\n", str_c(ident));
				str_truncate(ident, 0);
				
				strstart = p + 1;
				substart = strstart;
			}
			state = ST_NONE;
			p++;	
		}
	}

	//t_pop();
	
	if ( arglist == NULL ) {
		printf("STR: %s\n", strval);
		return sieve_validator_argument_activate_super
			(validator, cmd, *arg, TRUE);
	}
	
	if ( strend > strstart ) {
		struct sieve_ast_argument *strarg = _add_string_element(arglist, *arg);
		strarg->_value.str = str_new(pool, strend - strstart);
		str_append_n(strarg->_value.str, strstart, strend - strstart); 
		
		if ( !sieve_validator_argument_activate_super
			(validator, cmd, strarg, FALSE) )
			return FALSE;

		printf("STR: %s\n", t_strdup_until(strstart, substart));
	}	
	
	strdata = p_new(pool, struct _variable_string_data, 1);
	strdata->str_parts = arglist;
	
	(*arg)->context = (void *) strdata;

	return TRUE;
}

#define _string_data_first(data) __AST_LIST_FIRST((data)->str_parts)
#define _string_data_count(data) __AST_LIST_COUNT((data)->str_parts)
#define _string_data_next(item) __AST_LIST_NEXT(item)

static bool arg_variable_string_generate
(struct sieve_generator *generator, struct sieve_ast_argument *arg, 
	struct sieve_command_context *cmd) 
{
	struct sieve_binary *sbin = sieve_generator_get_binary(generator);
	struct _variable_string_data *strdata = 
		(struct _variable_string_data *) arg->context;
	struct sieve_ast_argument *strpart;
	
	if ( _string_data_count(strdata) == 1 )
		sieve_generate_argument(generator, _string_data_first(strdata), cmd);
	else {
		ext_variables_opr_variable_string_emit(sbin, _string_data_count(strdata));

		strpart = _string_data_first(strdata);
		while ( strpart != NULL ) {
			if ( !sieve_generate_argument(generator, strpart, cmd) )
				return FALSE;
			
			strpart = _string_data_next(strpart);
		}
	}
	
	return TRUE;
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
		sieve_code_dumpf(denv, "VARIABLE: %ld [?]", (long) index);

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
		sieve_variable_get(storage, index, str);

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

	*str = t_str_new(128);
	for ( i = 0; i < (unsigned int) elements; i++ ) {
		string_t *strelm;
		
		if ( !sieve_opr_string_read(renv, address, &strelm) ) 
			return FALSE;
		
		str_append_str(*str, strelm);
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


