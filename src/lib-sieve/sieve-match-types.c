/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */
 
#include <stdio.h>

#include "lib.h"
#include "compat.h"
#include "mempool.h"
#include "hash.h"
#include "array.h"

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-binary.h"
#include "sieve-comparators.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "sieve-match-types.h"

#include <string.h>

/*
 * Types
 */
 
struct sieve_match_values {
	pool_t pool;
	ARRAY_DEFINE(values, string_t *);
	unsigned count;
};

/* 
 * Default match types
 */ 

const struct sieve_match_type *sieve_core_match_types[] = {
	&is_match_type, &contains_match_type, &matches_match_type
};

const unsigned int sieve_core_match_types_count = 
	N_ELEMENTS(sieve_core_match_types);

/* 
 * Match-type 'extension' 
 */

static bool mtch_validator_load(struct sieve_validator *validator);

static int ext_my_id = -1;

const struct sieve_extension match_type_extension = {
	"@match-types",
	&ext_my_id,
	NULL, NULL,
	mtch_validator_load,
	NULL, NULL, NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_NO_OPERATIONS,
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

static const struct sieve_extension *ext_this = &match_type_extension;
	
/* 
 * Validator context:
 *   name-based match-type registry. 
 */
 
void sieve_match_type_register
(struct sieve_validator *validator, const struct sieve_match_type *mtch) 
{
	struct sieve_validator_object_registry *regs = 
		sieve_validator_object_registry_get(validator, ext_this);
	
	sieve_validator_object_registry_add(regs, &mtch->object);
}

const struct sieve_match_type *sieve_match_type_find
(struct sieve_validator *validator, const char *identifier) 
{
	struct sieve_validator_object_registry *regs = 
		sieve_validator_object_registry_get(validator, ext_this);
	const struct sieve_object *object = 
		sieve_validator_object_registry_find(regs, identifier);

  return (const struct sieve_match_type *) object;
}

bool mtch_validator_load(struct sieve_validator *validator)
{
	struct sieve_validator_object_registry *regs = 
		sieve_validator_object_registry_init(validator, ext_this);
	unsigned int i;

	/* Register core match-types */
	for ( i = 0; i < sieve_core_match_types_count; i++ ) {
		sieve_validator_object_registry_add
			(regs, &(sieve_core_match_types[i]->object));
	}

	return TRUE;
}

/* 
 * Interpreter context
 */

struct mtch_interpreter_context {
	struct sieve_match_values *match_values;
	bool match_values_enabled;
};

static void mtch_interpreter_free
(struct sieve_interpreter *interp ATTR_UNUSED, void *context)
{
	struct mtch_interpreter_context *mctx = 
		(struct mtch_interpreter_context *) context;
	
	if ( mctx->match_values != NULL ) {
		pool_unref(&mctx->match_values->pool);
	}
}

struct sieve_interpreter_extension mtch_interpreter_extension = {
	&match_type_extension,
	NULL,
	mtch_interpreter_free
};

static inline struct mtch_interpreter_context *
get_interpreter_context(struct sieve_interpreter *interp)
{
	return (struct mtch_interpreter_context *)
		sieve_interpreter_extension_get_context(interp, ext_this);
}

static struct mtch_interpreter_context *
mtch_interpreter_context_init(struct sieve_interpreter *interp)
{		
	pool_t pool = sieve_interpreter_pool(interp);
	struct mtch_interpreter_context *ctx;
	
	ctx = p_new(pool, struct mtch_interpreter_context, 1);

	sieve_interpreter_extension_register
		(interp, &mtch_interpreter_extension, (void *) ctx);

	return ctx;
}

/*
 * Match values
 */

bool sieve_match_values_set_enabled
(struct sieve_interpreter *interp, bool enable)
{
	struct mtch_interpreter_context *ctx = get_interpreter_context(interp);
	
	if ( ctx == NULL && enable ) 
		ctx = mtch_interpreter_context_init(interp);
	
	if ( ctx != NULL ) {
		bool previous = ctx->match_values_enabled;
		
		ctx->match_values_enabled = enable;
		return previous;
	}
	
	return FALSE;
}

bool sieve_match_values_are_enabled
(struct sieve_interpreter *interp)
{
	struct mtch_interpreter_context *ctx = get_interpreter_context(interp);
		
	return ( ctx == NULL ? FALSE : ctx->match_values_enabled );
}

struct sieve_match_values *sieve_match_values_start
(struct sieve_interpreter *interp)
{
	struct mtch_interpreter_context *ctx = get_interpreter_context(interp);
	struct sieve_match_values *match_values;
	
	if ( ctx == NULL || !ctx->match_values_enabled )
		return NULL;
	
	pool_t pool = pool_alloconly_create("sieve_match_values", 1024);
		
	match_values = p_new(pool, struct sieve_match_values, 1);
	match_values->pool = pool;
	match_values->count = 0;
	
	p_array_init(&match_values->values, pool, 4);

	return match_values;
}

static string_t *sieve_match_values_add_entry
(struct sieve_match_values *mvalues) 
{
	string_t *entry;
	
	if ( mvalues == NULL ) return NULL;	

	if ( mvalues->count >= SIEVE_MAX_MATCH_VALUES ) return NULL;
		
	if ( mvalues->count >= array_count(&mvalues->values) ) {
		entry = str_new(mvalues->pool, 64);
		array_append(&mvalues->values, &entry, 1);	} else {
		string_t * const *ep = array_idx(&mvalues->values, mvalues->count);
		entry = *ep;
		str_truncate(entry, 0);
	}
	
	mvalues->count++;

	return entry;
}

void sieve_match_values_set
(struct sieve_match_values *mvalues, unsigned int index, string_t *value)
{
	if ( mvalues != NULL && index < array_count(&mvalues->values) ) {
		string_t * const *ep = array_idx(&mvalues->values, index);
    	string_t *entry = *ep;

	    if ( entry != NULL && value != NULL ) {
			str_truncate(entry, 0);
        	str_append_str(entry, value);
		}
	}
}
	
void sieve_match_values_add
(struct sieve_match_values *mvalues, string_t *value) 
{
	string_t *entry = sieve_match_values_add_entry(mvalues); 

	if ( entry != NULL && value != NULL )
		str_append_str(entry, value);
}

void sieve_match_values_add_char
(struct sieve_match_values *mvalues, char c) 
{
	string_t *entry = sieve_match_values_add_entry(mvalues); 

	if ( entry != NULL )
		str_append_c(entry, c);
}

void sieve_match_values_skip
(struct sieve_match_values *mvalues, int num) 
{
	int i;
	
	for ( i = 0; i < num; i++ )
		(void) sieve_match_values_add_entry(mvalues); 
}

void sieve_match_values_commit
(struct sieve_interpreter *interp, struct sieve_match_values **mvalues)
{
	struct mtch_interpreter_context *ctx;
	
	if ( (*mvalues) == NULL ) return;
	
	ctx = get_interpreter_context(interp);
	if ( ctx == NULL || !ctx->match_values_enabled )
		return;	
		
	if ( ctx->match_values != NULL ) {
		pool_unref(&ctx->match_values->pool);
		ctx->match_values = NULL;
	}

	ctx->match_values = *mvalues;
	*mvalues = NULL;
}

void sieve_match_values_abort
(struct sieve_match_values **mvalues)
{		
	if ( (*mvalues) == NULL ) return;
	
	pool_unref(&(*mvalues)->pool);
	*mvalues = NULL;
}

void sieve_match_values_get
(struct sieve_interpreter *interp, unsigned int index, string_t **value_r) 
{
	struct mtch_interpreter_context *ctx = get_interpreter_context(interp);
	struct sieve_match_values *mvalues;

	if ( ctx == NULL || ctx->match_values == NULL ) {
		*value_r = NULL;
		return;
	}
	
	mvalues = ctx->match_values;
	if ( index < array_count(&mvalues->values) && index < mvalues->count ) {
		string_t * const *entry = array_idx(&mvalues->values, index);
		
		*value_r = *entry;
		return;
	}

	*value_r = NULL;	
}

/* 
 * Match-type tagged argument 
 */
 
/* Forward declarations */

static bool tag_match_type_is_instance_of
	(struct sieve_validator *validator, struct sieve_command_context *cmd, 
		struct sieve_ast_argument *arg);
static bool tag_match_type_validate
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
		struct sieve_command_context *cmd);
static bool tag_match_type_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
		struct sieve_command_context *cmd);

/* Argument object */
 
const struct sieve_argument match_type_tag = { 
	"MATCH-TYPE",
	tag_match_type_is_instance_of,
	NULL, 
	tag_match_type_validate, 
	NULL,
	tag_match_type_generate 
};

/* Argument implementation */

static bool tag_match_type_is_instance_of
(struct sieve_validator *validator, struct sieve_command_context *cmd, 
	struct sieve_ast_argument *arg)
{
	struct sieve_match_type_context *mtctx;
	const struct sieve_match_type *mtch = 
		sieve_match_type_find(validator, sieve_ast_argument_tag(arg));
		
	if ( mtch == NULL ) return FALSE;	
		
	/* Create context */
	mtctx = p_new(sieve_command_pool(cmd), struct sieve_match_type_context, 1);
	mtctx->match_type = mtch;
	mtctx->match_type_arg = arg;
	mtctx->command_ctx = cmd;
	mtctx->comparator = NULL; /* Can be filled in later */
	
	arg->context = (void *) mtctx;
	
	return TRUE;
}
 
static bool tag_match_type_validate
(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	struct sieve_match_type_context *mtctx = 
		(struct sieve_match_type_context *) (*arg)->context;
	const struct sieve_match_type *mtch = mtctx->match_type;

	/* Syntax:   
	 *   ":is" / ":contains" / ":matches" (subject to extension)
	 */
		
	/* Skip tag */
	*arg = sieve_ast_argument_next(*arg);
	
	/* Check whether this match type requires additional validation. 
	 * Additional validation can override the match type recorded in the context 
	 * for later code generation. 
	 */
	if ( mtch->validate != NULL ) {
		return mtch->validate(validator, arg, mtctx);
	}
	
	return TRUE;
}

static bool tag_match_type_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	struct sieve_match_type_context *mtctx =
		(struct sieve_match_type_context *) arg->context;
	
	(void) sieve_opr_match_type_emit(cgenv->sbin, mtctx->match_type);
			
	return TRUE;
}

void sieve_match_types_link_tags
	(struct sieve_validator *validator, 
		struct sieve_command_registration *cmd_reg, int id_code) 
{	
	sieve_validator_register_tag
		(validator, cmd_reg, &match_type_tag, id_code); 	
}

/*
 * Validation
 */

bool sieve_match_type_validate
(struct sieve_validator *validator, struct sieve_command_context *cmd,
	struct sieve_ast_argument *key_arg, 
	const struct sieve_match_type *mcht_default, 
	const struct sieve_comparator *cmp_default)
{
	struct sieve_ast_argument *arg = sieve_command_first_argument(cmd);
	struct sieve_ast_argument *mt_arg = NULL;
	struct sieve_match_type_context *mtctx;
	const struct sieve_match_type *mcht = NULL;
	const struct sieve_comparator *cmp = NULL;

	/* Find match type and comparator among the arguments */
	while ( arg != NULL && arg != cmd->first_positional ) {
		if ( sieve_argument_is_comparator(arg) ) {
			cmp = sieve_comparator_tag_get(arg);
			if ( mt_arg != NULL ) break;
		}

		if ( sieve_argument_is_match_type(arg) ) {
			mt_arg = arg;
			if ( cmp != NULL ) break;
		}
		arg = sieve_ast_argument_next(arg);
	}
	
	/* Verify using the default comparator if none is specified explicitly */
	if ( cmp == NULL )
		cmp = cmp_default;
	
	/* Verify the default match type if none is specified explicitly */
	if ( mt_arg == NULL || mt_arg->context == NULL ) {
		mtctx = NULL;
		mcht = mcht_default;
	} else {
		mtctx = (struct sieve_match_type_context *) mt_arg->context;
		mcht = mtctx->match_type;
		mtctx->comparator = cmp;
	}

	/* Check whether this match type requires additional validation. 
	 * Additional validation can override the match type recorded in the context 
	 * for later code generation. 
	 */
	if ( mcht != NULL && mcht->validate_context != NULL ) {
		return mcht->validate_context(validator, mt_arg, mtctx, key_arg);
	}
	
	return TRUE;	
}

/*
 * Match-type operand
 */
 
const struct sieve_operand_class sieve_match_type_operand_class = 
	{ "match type" };
	
static const struct sieve_extension_objects core_match_types =
	SIEVE_EXT_DEFINE_MATCH_TYPES(sieve_core_match_types);

const struct sieve_operand match_type_operand = { 
	"match-type", 
	NULL,
	SIEVE_OPERAND_MATCH_TYPE,
	&sieve_match_type_operand_class,
	&core_match_types
};

/*
 * Common validation implementation
 */

bool sieve_match_substring_validate_context
(struct sieve_validator *validator, struct sieve_ast_argument *arg,
	struct sieve_match_type_context *ctx,
	struct sieve_ast_argument *key_arg ATTR_UNUSED)
{
	const struct sieve_comparator *cmp = ctx->comparator;
		
	if ( cmp == NULL )
		return TRUE;
			
	if ( (cmp->flags & SIEVE_COMPARATOR_FLAG_SUBSTRING_MATCH) == 0 ) {
		sieve_argument_validate_error(validator, arg,
			"the specified %s comparator does not support "
			"sub-string matching as required by the :%s match type",
			cmp->object.identifier, ctx->match_type->object.identifier );

		return FALSE;
	}
	
	return TRUE;
} 
