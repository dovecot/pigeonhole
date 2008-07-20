/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file 
 */

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

/* 
 * Core comparators
 */
 
const struct sieve_comparator *sieve_core_comparators[] = {
	&i_octet_comparator, &i_ascii_casemap_comparator
};

const unsigned int sieve_core_comparators_count =
	N_ELEMENTS(sieve_core_comparators);

/*
 * Forward declarations
 */
 
static void sieve_opr_comparator_emit
	(struct sieve_binary *sbin, const struct sieve_comparator *cmp, int ext_id);

/* 
 * Comparator 'extension' 
 */

static int ext_my_id = -1;

static bool cmp_extension_load(int ext_id);
static bool cmp_validator_load(struct sieve_validator *validator);

const struct sieve_extension comparator_extension = {
	"@comparators",
	cmp_extension_load,
	cmp_validator_load,
	NULL, NULL, NULL, NULL,	NULL,
	SIEVE_EXT_DEFINE_NO_OPERATIONS,
	SIEVE_EXT_DEFINE_NO_OPERANDS    /* Defined as core operand */
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
	
	hash_insert
		(ctx->registrations, (void *) cmp->object.identifier, (void *) reg);
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
 * Comparator tagged argument 
 */
 
/* Context associated with ast argument */

struct sieve_comparator_context {
	struct sieve_command_context *command_ctx;
	const struct sieve_comparator *comparator;
	
	int ext_id;
};
 
/* Comparator argument */

static bool tag_comparator_validate
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd);
static bool tag_comparator_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
	struct sieve_command_context *cmd);

const struct sieve_argument comparator_tag = { 
	"comparator", 
	NULL, NULL,
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

static bool tag_comparator_generate
(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
	struct sieve_command_context *cmd ATTR_UNUSED)
{
	struct sieve_comparator_context *cmpctx = 
		(struct sieve_comparator_context *) arg->context;
	const struct sieve_comparator *cmp = cmpctx->comparator;
	
	sieve_opr_comparator_emit(cgenv->sbin, cmp, cmpctx->ext_id);
		
	return TRUE;
}

/* Functions to enable and evaluate comparator tag for commands */

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
	const struct sieve_comparator_context *cmpctx;
	
	if ( tag->argument != &comparator_tag ) 
		return NULL;
		
	cmpctx = (const struct sieve_comparator_context *) tag->context;
		 
	return cmpctx->comparator;
}

/*
 * Comparator coding
 */
 
const struct sieve_operand_class sieve_comparator_operand_class = 
	{ "COMPARATOR" };
	
static const struct sieve_extension_obj_registry core_comparators =
	SIEVE_EXT_DEFINE_COMPARATORS(sieve_core_comparators);

const struct sieve_operand comparator_operand = { 
	"comparator", 
	NULL,
	SIEVE_OPERAND_COMPARATOR, 
	&sieve_comparator_operand_class,
	&core_comparators
};

/*
 * Trivial/Common comparator method implementations
 */

bool sieve_comparator_octet_skip
	(const struct sieve_comparator *cmp ATTR_UNUSED, 
		const char **val, const char *val_end)
{
	if ( *val < val_end ) {
		(*val)++;
		return TRUE;
	}
	
	return FALSE;
}
