/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */
 
#include "lib.h"
#include "str.h"
 
#include "sieve-common.h"
#include "sieve-dump.h"
#include "sieve-binary.h"
#include "sieve-code.h"

#include "ext-variables-common.h"
#include "ext-variables-dump.h"

/*
 * Code dump context
 */
 
struct ext_variables_dump_context {
	struct sieve_variable_scope *main_scope;
	ARRAY_DEFINE(ext_scopes, struct sieve_variable_scope *);
};

static struct ext_variables_dump_context *ext_variables_dump_get_context
	(const struct sieve_dumptime_env *denv)
{
	struct sieve_code_dumper *dumper = denv->cdumper;
	struct ext_variables_dump_context *dctx = sieve_dump_extension_get_context
		(dumper, &variables_extension);
	pool_t pool;

	if ( dctx == NULL ) {
		/* Create dumper context */
		pool = sieve_code_dumper_pool(dumper);
		dctx = p_new(pool, struct ext_variables_dump_context, 1);
		p_array_init(&dctx->ext_scopes, pool, sieve_extensions_get_count());
	
		sieve_dump_extension_set_context(dumper, &variables_extension, dctx);
	}

	return dctx;
} 
 
bool ext_variables_code_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	struct sieve_code_dumper *dumper = denv->cdumper;
	struct ext_variables_dump_context *dctx;
	struct sieve_variable_scope *main_scope;
	unsigned int i, scope_size;
	sieve_size_t pc;
	int end_offset;
	
	sieve_code_mark(denv);
	if ( !sieve_binary_read_unsigned(denv->sbin, address, &scope_size) )
		return FALSE;
		
	pc = *address;	
	if ( !sieve_binary_read_offset(denv->sbin, address, &end_offset) )
		return FALSE;
	
	/* FIXME: MEMLEAK!! Scope is never unreferenced */
	main_scope = sieve_variable_scope_create(NULL);
	
	sieve_code_dumpf(denv, "SCOPE [%u] (end: %08x)", 
		scope_size, (unsigned int) (pc + end_offset));
	
	/* Read main variable scope */
	
	for ( i = 0; i < scope_size; i++ ) {
		string_t *identifier;

		sieve_code_mark(denv);
		if (!sieve_binary_read_string(denv->sbin, address, &identifier) ) {
			return FALSE;
		}
		
		sieve_code_dumpf(denv, "%3d: '%s'", i, str_c(identifier));
		
		(void) sieve_variable_scope_declare(main_scope, str_c(identifier));
	}
	
	dctx = ext_variables_dump_get_context(denv);
	dctx->main_scope = main_scope;
	
	return TRUE;
}

/*
 * Scope registry
 */

void sieve_ext_variables_dump_set_scope
(const struct sieve_dumptime_env *denv, const struct sieve_extension *ext, 
	struct sieve_variable_scope *scope)
{
	struct ext_variables_dump_context *dctx = ext_variables_dump_get_context(denv);

	array_idx_set(&dctx->ext_scopes, (unsigned int) *ext->id, &scope);	
}

/*
 * Variable identifier dump
 */

const char *ext_variables_dump_get_identifier
(const struct sieve_dumptime_env *denv, const struct sieve_extension *ext,
	unsigned int index)
{
	struct ext_variables_dump_context *dctx = ext_variables_dump_get_context(denv);	
	struct sieve_variable_scope *scope;
	struct sieve_variable *var;

	if ( ext == NULL )
		scope = dctx->main_scope;
	else {
		struct sieve_variable_scope *const *ext_scope;
		int ext_id = *ext->id;

		if  ( ext_id < 0 || ext_id >= (int) array_count(&dctx->ext_scopes) )
			return NULL;
	
		ext_scope = array_idx(&dctx->ext_scopes, (unsigned int) ext_id);
		scope = *ext_scope;			
	}

	if ( scope == NULL )
		return NULL;
			
	var = sieve_variable_scope_get_indexed(scope, index);
	
	return var->identifier;
}

