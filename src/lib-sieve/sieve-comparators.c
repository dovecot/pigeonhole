/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file 
 */

#include "lib.h"
#include "str-sanitize.h"
#include "hash.h"
#include "array.h"

#include "sieve-extensions.h"
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
	(struct sieve_binary *sbin, const struct sieve_comparator *cmp);

/* 
 * Comparator 'extension' 
 */

static int ext_my_id = -1;

static bool cmp_extension_load(int ext_id);
static bool cmp_validator_load(struct sieve_validator *validator);

const struct sieve_extension comparator_extension = {
	"@comparators",
	&ext_my_id,
	cmp_extension_load,
	NULL,
	cmp_validator_load,
	NULL, NULL, NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_NO_OPERATIONS,
	SIEVE_EXT_DEFINE_NO_OPERANDS    /* Defined as core operand */
};

static const struct sieve_extension *ext_this = &comparator_extension;
	
static bool cmp_extension_load(int ext_id) 
{
	ext_my_id = ext_id;
	return TRUE;
}

/* 
 * Validator context:
 *   name-based comparator registry. 
 */
 
void sieve_comparator_register
(struct sieve_validator *validator, const struct sieve_comparator *cmp) 
{
	struct sieve_validator_object_registry *regs = 
		sieve_validator_object_registry_get(validator, ext_this);
	
	sieve_validator_object_registry_add(regs, &cmp->object);
}

const struct sieve_comparator *sieve_comparator_find
(struct sieve_validator *validator, const char *identifier) 
{
	struct sieve_validator_object_registry *regs = 
		sieve_validator_object_registry_get(validator, ext_this);
	const struct sieve_object *object = 
		sieve_validator_object_registry_find(regs, identifier);

  return (const struct sieve_comparator *) object;
}

bool cmp_validator_load(struct sieve_validator *validator)
{
	struct sieve_validator_object_registry *regs = 
		sieve_validator_object_registry_init(validator, ext_this);
	unsigned int i;
		
	/* Register core comparators */
	for ( i = 0; i < sieve_core_comparators_count; i++ ) {
		sieve_validator_object_registry_add
			(regs, &(sieve_core_comparators[i]->object));
	}

	return TRUE;
}

/* 
 * Comparator tagged argument 
 */
 
/* Context data */

struct sieve_comparator_context {
	struct sieve_command_context *command_ctx;
	const struct sieve_comparator *comparator;
};
 
/* Forward declarations */

static bool tag_comparator_validate
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd);
static bool tag_comparator_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_ast_argument *arg, 
	struct sieve_command_context *cmd);

/* Argument object */

const struct sieve_argument comparator_tag = { 
	"comparator", 
	NULL, NULL,
	tag_comparator_validate, 
	NULL,
	tag_comparator_generate 
};

/* Argument implementation */

static bool tag_comparator_validate
	(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
	struct sieve_command_context *cmd)
{
	struct sieve_comparator_context *cmpctx;
	struct sieve_ast_argument *tag = *arg;
	const struct sieve_comparator *cmp;
	
	/* Skip tag */
	*arg = sieve_ast_argument_next(*arg);
	
	/* Check syntax:
	 *   ":comparator" <comparator-name: string>
	 */
	if ( (*arg)->type != SAAT_STRING ) {
		sieve_argument_validate_error(validator, *arg, 
			":comparator tag requires one string argument, but %s was found", 
			sieve_ast_argument_name(*arg) );
		return FALSE;
	}

	if ( !sieve_validator_argument_activate(validator, cmd, *arg, FALSE) )
		return FALSE;

	/* FIXME: We can currently only handle string literal argument, so
	 * variables are not allowed.
	 */
	if ( !sieve_argument_is_string_literal(*arg) ) {
		sieve_argument_validate_error(validator, *arg, 
			"this Sieve implementation currently only supports "
			"a literal string argument for the :comparator tag");
		return FALSE;
	}
	
	/* Get comparator from registry */
	cmp = sieve_comparator_find(validator, sieve_ast_argument_strc(*arg));
	
	if ( cmp == NULL ) {
		sieve_argument_validate_error(validator, *arg, 
			"unknown comparator '%s'", 
			str_sanitize(sieve_ast_argument_strc(*arg),80));

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
	
	sieve_opr_comparator_emit(cgenv->sbin, cmp);
		
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
	{ "comparator" };
	
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
