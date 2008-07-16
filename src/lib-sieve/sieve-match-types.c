#include <stdio.h>

#include "lib.h"
#include "compat.h"
#include "mempool.h"
#include "hash.h"
#include "array.h"

#include "sieve-extensions-private.h"
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
 * Default match types
 */ 

const struct sieve_match_type *sieve_core_match_types[] = {
	&is_match_type, &contains_match_type, &matches_match_type
};

const unsigned int sieve_core_match_types_count = 
	N_ELEMENTS(sieve_core_match_types);

static struct sieve_extension_obj_registry mtch_default_reg =
	SIEVE_EXT_DEFINE_MATCH_TYPES(sieve_core_match_types);

/* 
 * Forward declarations 
 */
  
static void opr_match_type_emit
	(struct sieve_binary *sbin, const struct sieve_match_type *mtch, int ext_id);

/* 
 * Match-type 'extension' 
 */

static int ext_my_id = -1;

static bool mtch_extension_load(int ext_id);
static bool mtch_validator_load(struct sieve_validator *validator);
static bool mtch_binary_load(struct sieve_binary *sbin);

const struct sieve_extension match_type_extension = {
	"@match-types",
	mtch_extension_load,
	mtch_validator_load,
	NULL, NULL, NULL,
	mtch_binary_load,
	NULL,
	SIEVE_EXT_DEFINE_NO_OPERATIONS,
	SIEVE_EXT_DEFINE_NO_OPERANDS
};
	
static bool mtch_extension_load(int ext_id) 
{
	ext_my_id = ext_id;
	return TRUE;
}

/* 
 * Validator context:
 *   name-based match-type registry. 
 *
 * FIXME: This code will be duplicated across all extensions that introduce 
 * a registry of some kind in the validator. 
 */
 
struct mtch_validator_registration {
	int ext_id;
	const struct sieve_match_type *match_type;
};
 
struct mtch_validator_context {
	struct hash_table *registrations;
};

static inline struct mtch_validator_context *
	get_validator_context(struct sieve_validator *validator)
{
	return (struct mtch_validator_context *) 
		sieve_validator_extension_get_context(validator, ext_my_id);
}

static void _sieve_match_type_register
	(pool_t pool, struct mtch_validator_context *ctx, 
	const struct sieve_match_type *mtch, int ext_id) 
{
	struct mtch_validator_registration *reg;
	
	reg = p_new(pool, struct mtch_validator_registration, 1);
	reg->match_type = mtch;
	reg->ext_id = ext_id;
	
	hash_insert(ctx->registrations, (void *) mtch->identifier, (void *) reg);
}
 
void sieve_match_type_register
	(struct sieve_validator *validator, 
	const struct sieve_match_type *mtch, int ext_id) 
{
	pool_t pool = sieve_validator_pool(validator);
	struct mtch_validator_context *ctx = get_validator_context(validator);

	_sieve_match_type_register(pool, ctx, mtch, ext_id);
}

const struct sieve_match_type *sieve_match_type_find
	(struct sieve_validator *validator, const char *identifier,
		int *ext_id) 
{
	struct mtch_validator_context *ctx = get_validator_context(validator);
	struct mtch_validator_registration *reg =
		(struct mtch_validator_registration *) 
			hash_lookup(ctx->registrations, identifier);
			
	if ( reg == NULL ) return NULL;

	if ( ext_id != NULL ) *ext_id = reg->ext_id;

  return reg->match_type;
}

bool mtch_validator_load(struct sieve_validator *validator)
{
	unsigned int i;
	pool_t pool = sieve_validator_pool(validator);
	
	struct mtch_validator_context *ctx = 
		p_new(pool, struct mtch_validator_context, 1);
	
	/* Setup match-type registry */
	ctx->registrations = hash_create
		(pool, pool, 0, str_hash, (hash_cmp_callback_t *)strcmp);

	/* Register core match-types */
	for ( i = 0; i < sieve_core_match_types_count; i++ ) {
		const struct sieve_match_type *mtch = sieve_core_match_types[i];
		
		_sieve_match_type_register(pool, ctx, mtch, -1);
	}

	sieve_validator_extension_set_context(validator, ext_my_id, ctx);

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
 * Binary context
 */

static inline const struct sieve_match_type_extension *sieve_match_type_extension_get
	(struct sieve_binary *sbin, int ext_id)
{	
	return (const struct sieve_match_type_extension *)
		sieve_binary_registry_get_object(sbin, ext_my_id, ext_id);
}

void sieve_match_type_extension_set
	(struct sieve_binary *sbin, int ext_id,
		const struct sieve_match_type_extension *ext)
{
	sieve_binary_registry_set_object
		(sbin, ext_my_id, ext_id, (const void *) ext);
}

static bool mtch_binary_load(struct sieve_binary *sbin)
{
	sieve_binary_registry_init(sbin, ext_my_id);
	
	return TRUE;
}

/* 
 * Interpreter context
 */

struct mtch_interpreter_context {
	struct sieve_match_values *match_values;
	bool match_values_enabled;
};

static inline struct mtch_interpreter_context *
get_interpreter_context(struct sieve_interpreter *interp)
{
	return (struct mtch_interpreter_context *)
		sieve_interpreter_extension_get_context(interp, ext_my_id);
}

static struct mtch_interpreter_context *
mtch_interpreter_context_init(struct sieve_interpreter *interp)
{		
	pool_t pool = sieve_interpreter_pool(interp);
	struct mtch_interpreter_context *ctx;
	
	ctx = p_new(pool, struct mtch_interpreter_context, 1);

	sieve_interpreter_extension_set_context
		(interp, ext_my_id, (void *) ctx);

	return ctx;
}

/*
 * Match values
 */
 
struct sieve_match_values {
	pool_t pool;
	ARRAY_DEFINE(values, string_t *);
	unsigned count;
};

bool sieve_match_values_set_enabled
(struct sieve_interpreter *interp, bool enable)
{
	bool previous;
	struct mtch_interpreter_context *ctx = get_interpreter_context(interp);
	
	if ( ctx == NULL && enable ) 
		ctx = mtch_interpreter_context_init(interp);
	
	previous = ctx->match_values_enabled;
	ctx->match_values_enabled = enable;
	
	return previous;
}

struct sieve_match_values *sieve_match_values_start(struct sieve_interpreter *interp)
{
	struct mtch_interpreter_context *ctx = get_interpreter_context(interp);
	
	if ( ctx == NULL || !ctx->match_values_enabled )
		return NULL;
		
	if ( ctx->match_values == NULL ) {
		pool_t pool = sieve_interpreter_pool(interp);
		
		ctx->match_values = p_new(pool, struct sieve_match_values, 1);
		ctx->match_values->pool = pool;
		p_array_init(&ctx->match_values->values, pool, 4);
	}
	
	ctx->match_values->count = 0;
	
	return ctx->match_values;
}

static string_t *sieve_match_values_add_entry
	(struct sieve_match_values *mvalues) 
{
	string_t *entry;
	
	if ( mvalues == NULL ) return NULL;	
		
	if ( mvalues->count >= array_count(&mvalues->values) ) {
		entry = str_new(mvalues->pool, 64);
		array_append(&mvalues->values, &entry, 1);
	} else {
		string_t * const *ep = array_idx(&mvalues->values, mvalues->count);
		entry = *ep;
		str_truncate(entry, 0);
	}
	
	mvalues->count++;

	return entry;
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
 * Match-type operand
 */
 
struct sieve_operand_class match_type_class = 
	{ "match-type" };

struct sieve_operand match_type_operand = { 
	"match-type", 
	NULL,
	SIEVE_OPERAND_MATCH_TYPE,
	&match_type_class,
	NULL
};

/* 
 * Match-type tag 
 */
  
static bool tag_match_type_is_instance_of
(struct sieve_validator *validator, struct sieve_command_context *cmd, 
	struct sieve_ast_argument *arg)
{
	int ext_id;
	struct sieve_match_type_context *mtctx;
	const struct sieve_match_type *mtch = 
		sieve_match_type_find(validator, sieve_ast_argument_tag(arg), &ext_id);
		
	if ( mtch == NULL ) return FALSE;	
		
	/* Create context */
	mtctx = p_new(sieve_command_pool(cmd), struct sieve_match_type_context, 1);
	mtctx->match_type = mtch;
	mtctx->ext_id = ext_id;
	mtctx->command_ctx = cmd;
	
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

bool sieve_match_type_validate_argument
(struct sieve_validator *validator, struct sieve_ast_argument *arg,
	struct sieve_ast_argument *key_arg )
{
	struct sieve_match_type_context *mtctx = 
		(struct sieve_match_type_context *) arg->context;

	i_assert(arg->argument == &match_type_tag);

	/* Check whether this match type requires additional validation. 
	 * Additional validation can override the match type recorded in the context 
	 * for later code generation. 
	 */
	if ( mtctx != NULL && mtctx->match_type != NULL &&
		mtctx->match_type->validate_context != NULL ) {
		return mtctx->match_type->validate_context(validator, arg, mtctx, key_arg);
	}
	
	return TRUE;
}

bool sieve_match_type_validate
(struct sieve_validator *validator, struct sieve_command_context *cmd,
	struct sieve_ast_argument *key_arg )
{
	struct sieve_ast_argument *arg = sieve_command_first_argument(cmd);

	while ( arg != NULL && arg != cmd->first_positional ) {
		if ( arg->argument == &match_type_tag ) {
			if ( !sieve_match_type_validate_argument(validator, arg, key_arg) ) 
				return FALSE;
		}
		arg = sieve_ast_argument_next(arg);
	}

	return TRUE;	
}

/* 
 * Code generation 
 */

static void opr_match_type_emit
	(struct sieve_binary *sbin, const struct sieve_match_type *mtch, int ext_id)
{ 
	(void) sieve_operand_emit_code(sbin, &match_type_operand, -1);	
	
	(void) sieve_extension_emit_obj
		(sbin, &mtch_default_reg, mtch, match_types, ext_id);
}

static const struct sieve_extension_obj_registry *
	sieve_match_type_registry_get
(struct sieve_binary *sbin, unsigned int ext_index)
{
	int ext_id = -1; 
	const struct sieve_match_type_extension *ext;
	
	if ( sieve_binary_extension_get_by_index(sbin, ext_index, &ext_id) == NULL )
		return NULL;

	if ( (ext=sieve_match_type_extension_get(sbin, ext_id)) == NULL ) 
		return NULL;
		
	return &(ext->match_types);
}

static inline const struct sieve_match_type *sieve_match_type_read
  (struct sieve_binary *sbin, sieve_size_t *address)
{
	return sieve_extension_read_obj
		(struct sieve_match_type, sbin, address, &mtch_default_reg, 
			sieve_match_type_registry_get);
}

const struct sieve_match_type *sieve_opr_match_type_read
  (const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	const struct sieve_operand *operand = sieve_operand_read(renv->sbin, address);
	
	if ( operand == NULL || operand->class != &match_type_class ) 
		return NULL;

	return sieve_match_type_read(renv->sbin, address);
}

bool sieve_opr_match_type_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	const struct sieve_operand *operand = sieve_operand_read(denv->sbin, address);
	const struct sieve_match_type *mtch;
	
	if ( operand == NULL || operand->class != &match_type_class ) 
		return NULL;

	sieve_code_mark(denv); 
	mtch = sieve_match_type_read(denv->sbin, address);
	
	if ( mtch == NULL )
		return FALSE;
		
	sieve_code_dumpf(denv, "MATCH-TYPE: %s", mtch->identifier);
	
	return TRUE;
}

static bool tag_match_type_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	struct sieve_match_type_context *mtctx =
		(struct sieve_match_type_context *) arg->context;
	
	if ( mtctx->match_type->extension == NULL ) {
		if ( mtctx->match_type->code < SIEVE_MATCH_TYPE_CUSTOM )
			opr_match_type_emit(cgenv->sbin, mtctx->match_type, -1);
		else
			return FALSE;
	} else {
		opr_match_type_emit(cgenv->sbin, mtctx->match_type, mtctx->ext_id);
	} 
			
	return TRUE;
}

/* Match Utility */

struct sieve_match_context *sieve_match_begin
(struct sieve_interpreter *interp, const struct sieve_match_type *mtch, 
	const struct sieve_comparator *cmp, struct sieve_coded_stringlist *key_list)
{
	struct sieve_match_context *mctx = t_new(struct sieve_match_context, 1);  

	mctx->interp = interp;
	mctx->match_type = mtch;
	mctx->comparator = cmp;
	mctx->key_list = key_list;

	if ( mtch->match_init != NULL ) {
		mtch->match_init(mctx);
	}

	return mctx;
}

bool sieve_match_value
	(struct sieve_match_context *mctx, const char *value, size_t val_size)
{
	const struct sieve_match_type *mtch = mctx->match_type;
	sieve_coded_stringlist_reset(mctx->key_list);
				
	/* Reject unimplemented match-type */
	if ( mtch->match == NULL )
		return FALSE;
				
	/* Match to all key values */
	if ( mtch->is_iterative ) {
		unsigned int key_index = 0;
		string_t *key_item = NULL;
	
		while ( sieve_coded_stringlist_next_item(mctx->key_list, &key_item) && 
			key_item != NULL ) 
		{
			if ( mtch->match
				(mctx, value, val_size, str_c(key_item), 
					str_len(key_item), key_index) ) {
				return TRUE;  
			}
	
			key_index++;
		}
	} else {
		if ( mtch->match(mctx, value, strlen(value), NULL, 0, -1) )
			return TRUE;  
	}

	return FALSE;
}

bool sieve_match_end(struct sieve_match_context *mctx)
{
	const struct sieve_match_type *mtch = mctx->match_type;

	if ( mtch->match_deinit != NULL ) {
		return mtch->match_deinit(mctx);
	}

	return FALSE;
}

/*
 * Matching
 */

bool sieve_match_substring_validate_context
(struct sieve_validator *validator, struct sieve_ast_argument *arg,
    struct sieve_match_type_context *ctx, 
	struct sieve_ast_argument *key_arg ATTR_UNUSED)
{
	struct sieve_ast_argument *carg =
		sieve_command_first_argument(ctx->command_ctx);

    while ( carg != NULL && carg != ctx->command_ctx->first_positional ) {
		if ( carg != arg && carg->argument == &comparator_tag ) {
			const struct sieve_comparator *cmp =
				sieve_comparator_tag_get(carg);
			
			if ( (cmp->flags & SIEVE_COMPARATOR_FLAG_SUBSTRING_MATCH) == 0 ) {
                sieve_command_validate_error(validator, ctx->command_ctx,
                    "the specified %s comparator does not support "
					"sub-string matching as required by the :%s match type",
					cmp->identifier, ctx->match_type->identifier );

				return FALSE;
			}
            return TRUE;
		}

		carg = sieve_ast_argument_next(carg);
	}

	return TRUE;
}

/* 
 * Core match-type modifiers
 */

const struct sieve_argument match_type_tag = { 
	"MATCH-TYPE",
	tag_match_type_is_instance_of,
	NULL, 
	tag_match_type_validate, 
	NULL,
	tag_match_type_generate 
};
 
