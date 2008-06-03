#include "lib.h"
#include "compat.h"
#include "hash.h"
#include "array.h"

#include "sieve-extensions-private.h"
#include "sieve-code.h"
#include "sieve-commands.h"
#include "sieve-binary.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "sieve-comparators.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* 
 * Default comparators
 */
 
const struct sieve_comparator *sieve_core_comparators[] = {
	&i_octet_comparator, &i_ascii_casemap_comparator
};

const unsigned int sieve_core_comparators_count =
	N_ELEMENTS(sieve_core_comparators);

static struct sieve_extension_obj_registry cmp_default_reg =
	SIEVE_EXT_DEFINE_COMPARATORS(sieve_core_comparators);

/* 
 * Forward declarations 
 */
 
static void opr_comparator_emit
	(struct sieve_binary *sbin, const struct sieve_comparator *cmp, int ext_id);

static int cmp_i_octet_compare
	(const struct sieve_comparator *cmp,
		const char *val1, size_t val1_size, const char *val2, size_t val2_size);
static bool cmp_i_octet_char_match
	(const struct sieve_comparator *cmp, const char **val1, const char *val1_end, 
		const char **val2, const char *val2_end);
static bool cmp_i_octet_char_skip
	(const struct sieve_comparator *cmp, const char **val, const char *val_end);
	
static int cmp_i_ascii_casemap_compare
	(const struct sieve_comparator *cmp,
		const char *val1, size_t val1_size, const char *val2, size_t val2_size);
static bool cmp_i_ascii_casemap_char_match
	(const struct sieve_comparator *cmp, const char **val1, const char *val1_end, 
		const char **val2, const char *val2_end);

/* 
 * Comparator 'extension' 
 */

static int ext_my_id = -1;

static bool cmp_extension_load(int ext_id);
static bool cmp_validator_load(struct sieve_validator *validator);
static bool cmp_binary_load(struct sieve_binary *sbin);

const struct sieve_extension comparator_extension = {
	"@comparators",
	cmp_extension_load,
	cmp_validator_load,
	NULL, 
	NULL,
	cmp_binary_load,
	NULL,
	SIEVE_EXT_DEFINE_NO_OPERATIONS,
	SIEVE_EXT_DEFINE_NO_OPERANDS
};
	
static bool cmp_extension_load(int ext_id) 
{
	ext_my_id = ext_id;
	return TRUE;
}

/* 
 * Validator context:
 *   name-based comparator registry. 
 *
 * FIXME: This code will be duplicated across all extensions that introduce 
 * a registry of some kind in the validator. 
 */
 
struct cmp_validator_registration {
	int ext_id;
	const struct sieve_comparator *comparator;
};
 
struct cmp_validator_context {
	struct hash_table *registrations;
};

static inline struct cmp_validator_context *
	get_validator_context(struct sieve_validator *validator)
{
	return (struct cmp_validator_context *) 
		sieve_validator_extension_get_context(validator, ext_my_id);
}

static void _sieve_comparator_register
	(pool_t pool, struct cmp_validator_context *ctx, 
	const struct sieve_comparator *cmp, int ext_id) 
{
	struct cmp_validator_registration *reg;
	
	reg = p_new(pool, struct cmp_validator_registration, 1);
	reg->comparator = cmp;
	reg->ext_id = ext_id;
	
	hash_insert(ctx->registrations, (void *) cmp->identifier, (void *) reg);
}
 
void sieve_comparator_register
	(struct sieve_validator *validator, 
	const struct sieve_comparator *cmp, int ext_id) 
{
	pool_t pool = sieve_validator_pool(validator);
	struct cmp_validator_context *ctx = get_validator_context(validator);

	_sieve_comparator_register(pool, ctx, cmp, ext_id);
}

const struct sieve_comparator *sieve_comparator_find
	(struct sieve_validator *validator, const char *identifier,
		int *ext_id) 
{
	struct cmp_validator_context *ctx = get_validator_context(validator);
	struct cmp_validator_registration *reg =
		(struct cmp_validator_registration *) 
			hash_lookup(ctx->registrations, identifier);
			
	if ( reg == NULL ) return NULL;

	if ( ext_id != NULL ) *ext_id = reg->ext_id;

  return reg->comparator;
}

bool cmp_validator_load(struct sieve_validator *validator)
{
	unsigned int i;
	pool_t pool = sieve_validator_pool(validator);
	
	struct cmp_validator_context *ctx = 
		p_new(pool, struct cmp_validator_context, 1);
	
	/* Setup comparator registry */
	ctx->registrations = hash_create
		(pool, pool, 0, str_hash, (hash_cmp_callback_t *)strcmp);

	/* Register core comparators */
	for ( i = 0; i < sieve_core_comparators_count; i++ ) {
		const struct sieve_comparator *cmp = sieve_core_comparators[i];
		
		_sieve_comparator_register(pool, ctx, cmp, -1);
	}

	sieve_validator_extension_set_context(validator, ext_my_id, ctx);

	return TRUE;
}

/*
 * Binary context
 */

static inline const struct sieve_comparator_extension *
	sieve_comparator_extension_get(struct sieve_binary *sbin, int ext_id)
{
	return (const struct sieve_comparator_extension *)
		sieve_binary_registry_get_object(sbin, ext_my_id, ext_id);
}

void sieve_comparator_extension_set
	(struct sieve_binary *sbin, int ext_id,
		const struct sieve_comparator_extension *ext)
{
	sieve_binary_registry_set_object
		(sbin, ext_my_id, ext_id, (const void *) ext);
}

static bool cmp_binary_load(struct sieve_binary *sbin)
{
	sieve_binary_registry_init(sbin, ext_my_id);
	
	return TRUE;
}

/*
 * Comparator operand
 */
 
struct sieve_operand_class comparator_class = 
	{ "comparator" };

struct sieve_operand comparator_operand = { 
	"comparator", 
	NULL, SIEVE_OPERAND_COMPARATOR, 
	&comparator_class,
	NULL
};

/* 
 * Comparator tag 
 */
 
static bool tag_comparator_validate
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd);
static bool tag_comparator_generate
	(struct sieve_generator *generator, struct sieve_ast_argument *arg, 
	struct sieve_command_context *cmd);

const struct sieve_argument comparator_tag = { 
	"comparator", 
	NULL, 
	tag_comparator_validate, 
	NULL,
	tag_comparator_generate 
};

static bool tag_comparator_validate
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	int ext_id;
	struct sieve_comparator_context *cmpctx;
	struct sieve_ast_argument *tag = *arg;
	const struct sieve_comparator *cmp;
	
	/* Skip tag */
	*arg = sieve_ast_argument_next(*arg);
	
	/* Check syntax:
	 *   ":comparator" <comparator-name: string>
	 */
	if ( (*arg)->type != SAAT_STRING ) {
		sieve_command_validate_error(validator, cmd, 
			":comparator tag requires one string argument, but %s was found", 
			sieve_ast_argument_name(*arg) );
		return FALSE;
	}
	
	/* Get comparator from registry */
	cmp = sieve_comparator_find
		(validator, sieve_ast_argument_strc(*arg), &ext_id);
	
	if ( cmp == NULL ) {
		sieve_command_validate_error(validator, cmd, 
			"unknown comparator '%s'", sieve_ast_argument_strc(*arg));

		return FALSE;
	}
	
	/* String argument not needed during code generation, so detach it from 
	 * argument list 
	 */
	*arg = sieve_ast_arguments_detach(*arg, 1);

	/* Create context */
	cmpctx = p_new(sieve_command_pool(cmd), struct sieve_comparator_context, 1);
	cmpctx->command_ctx = cmd;
	cmpctx->comparator = cmp;
	cmpctx->ext_id = ext_id;

	/* Store comparator in context */
	tag->context = (void *) cmpctx;
	
	return TRUE;
}

void sieve_comparators_link_tag
	(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg,	
		int id_code) 
{
	sieve_validator_register_tag(validator, cmd_reg, &comparator_tag, id_code); 	
}

bool sieve_comparator_tag_is
(struct sieve_ast_argument *tag, const struct sieve_comparator *cmp)
{
	const struct sieve_comparator_context *cmpctx = 
		(const struct sieve_comparator_context *) tag->context;

	if ( cmpctx == NULL ) return FALSE;
	
	return ( tag->argument == &comparator_tag && cmpctx->comparator == cmp );
}

const struct sieve_comparator *sieve_comparator_tag_get
(struct sieve_ast_argument *tag)
{
	if ( tag->argument != &comparator_tag ) 
		return NULL;
		 
	return (const struct sieve_comparator *) tag->context;
}

/* Code generation */

static void opr_comparator_emit
	(struct sieve_binary *sbin, const struct sieve_comparator *cmp, int ext_id)
{ 
	(void) sieve_operand_emit_code(sbin, &comparator_operand, -1);	

	(void) sieve_extension_emit_obj
		(sbin, &cmp_default_reg, cmp, comparators, ext_id);
}

static const struct sieve_extension_obj_registry *
	sieve_comparator_registry_get
(struct sieve_binary *sbin, unsigned int ext_index)
{
	int ext_id = -1; 
	const struct sieve_comparator_extension *ext;
	
	if ( sieve_binary_extension_get_by_index(sbin, ext_index, &ext_id) == NULL )
		return NULL;

	if ( (ext=sieve_comparator_extension_get(sbin, ext_id)) == NULL ) 
		return NULL;
		
	return &(ext->comparators);
}

static inline const struct sieve_comparator *sieve_comparator_read
  (struct sieve_binary *sbin, sieve_size_t *address)
{
	return sieve_extension_read_obj
		(struct sieve_comparator, sbin, address, &cmp_default_reg, 
			sieve_comparator_registry_get);
}

const struct sieve_comparator *sieve_opr_comparator_read
  (const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	const struct sieve_operand *operand = sieve_operand_read(renv->sbin, address);
	
	if ( operand == NULL || operand->class != &comparator_class ) 
		return NULL;
	
	return sieve_comparator_read(renv->sbin, address);
}

bool sieve_opr_comparator_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	const struct sieve_operand *operand = sieve_operand_read(denv->sbin, address);
	const struct sieve_comparator *cmp;
	
	if ( operand == NULL || operand->class != &comparator_class ) 
		return NULL;

	sieve_code_mark(denv);
	cmp = sieve_comparator_read(denv->sbin, address);
	
	if ( cmp == NULL )
		return FALSE;
		
	sieve_code_dumpf(denv, "COMPARATOR: %s", cmp->identifier);
	
	return TRUE;
}

static bool tag_comparator_generate
	(struct sieve_generator *generator, struct sieve_ast_argument *arg, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	struct sieve_binary *sbin = sieve_generator_get_binary(generator);
	struct sieve_comparator_context *cmpctx = 
		(struct sieve_comparator_context *) arg->context;
	const struct sieve_comparator *cmp = cmpctx->comparator;
	
	if ( cmp->extension == NULL ) {
		if ( cmp->code < SIEVE_COMPARATOR_CUSTOM )
			opr_comparator_emit(sbin, cmp, -1);
		else
			return FALSE;
	} else {
		opr_comparator_emit(sbin, cmp, cmpctx->ext_id);
	} 
		
	return TRUE;
}

/* 
 * Core comparators  
 */

const struct sieve_comparator i_octet_comparator = {
	"i;octet",
	SIEVE_COMPARATOR_FLAG_ORDERING | SIEVE_COMPARATOR_FLAG_EQUALITY |
		SIEVE_COMPARATOR_FLAG_SUBSTRING_MATCH | SIEVE_COMPARATOR_FLAG_PREFIX_MATCH,
	NULL,
	SIEVE_COMPARATOR_I_OCTET,
	cmp_i_octet_compare,
	cmp_i_octet_char_match,
	cmp_i_octet_char_skip	
};

const struct sieve_comparator i_ascii_casemap_comparator = {
	"i;ascii-casemap",
	SIEVE_COMPARATOR_FLAG_ORDERING | SIEVE_COMPARATOR_FLAG_EQUALITY |
		SIEVE_COMPARATOR_FLAG_SUBSTRING_MATCH | SIEVE_COMPARATOR_FLAG_PREFIX_MATCH,
	NULL,
	SIEVE_COMPARATOR_I_ASCII_CASEMAP,
	cmp_i_ascii_casemap_compare,
	cmp_i_ascii_casemap_char_match,
	cmp_i_octet_char_skip
};

static int cmp_i_octet_compare(
	const struct sieve_comparator *cmp ATTR_UNUSED,
	const char *val1, size_t val1_size, const char *val2, size_t val2_size)
{
	int result;

	if ( val1_size == val2_size ) {
		return memcmp((void *) val1, (void *) val2, val1_size);
	} 
	
	if ( val1_size > val2_size ) {
		result = memcmp((void *) val1, (void *) val2, val2_size);
		
		if ( result == 0 ) return 1;
		
		return result;
	} 

	result = memcmp((void *) val1, (void *) val2, val1_size);
		
	if ( result == 0 ) return -1;
		
	return result;
}

static bool cmp_i_octet_char_match
	(const struct sieve_comparator *cmp ATTR_UNUSED, 
		const char **val, const char *val_end, 
		const char **key, const char *key_end)
{
	const char *val_begin = *val;
	const char *key_begin = *key;
	
	while ( **val == **key && *val < val_end && *key < key_end ) {
		(*val)++;
		(*key)++;
	}
	
	if ( *key < key_end ) {
		/* Reset */
		*val = val_begin;
		*key = key_begin;	
	
		return FALSE;
	}
	
	return TRUE;
}

static bool cmp_i_octet_char_skip
	(const struct sieve_comparator *cmp ATTR_UNUSED, 
		const char **val, const char *val_end)
{
	if ( *val < val_end ) {
		(*val)++;
		return TRUE;
	}
	
	return FALSE;
}
	
static int cmp_i_ascii_casemap_compare(
	const struct sieve_comparator *cmp ATTR_UNUSED,
	const char *val1, size_t val1_size, const char *val2, size_t val2_size)
{
	int result;

	if ( val1_size == val2_size ) {
		return strncasecmp(val1, val2, val1_size);
	} 
	
	if ( val1_size > val2_size ) {
		result = strncasecmp(val1, val2, val2_size);
		
		if ( result == 0 ) return 1;
		
		return result;
	} 

	result = strncasecmp(val1, val2, val1_size);
		
	if ( result == 0 ) return -1;
		
	return result;
}

static bool cmp_i_ascii_casemap_char_match
	(const struct sieve_comparator *cmp ATTR_UNUSED, 
		const char **val, const char *val_end, 
		const char **key, const char *key_end)
{
	const char *val_begin = *val;
	const char *key_begin = *key;
	
	while ( i_tolower(**val) == i_tolower(**key) &&
		*val < val_end && *key < key_end ) {
		(*val)++;
		(*key)++;
	}
	
	if ( *key < key_end ) {
		/* Reset */
		*val = val_begin;
		*key = key_begin;	
		
		return FALSE;
	}
	
	return TRUE;
}


