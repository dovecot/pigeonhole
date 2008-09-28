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
}; 
 
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
	
	/* Create dumper context */
	dctx = p_new(sieve_code_dumper_pool(dumper), 
		struct ext_variables_dump_context, 1);
	dctx->main_scope = main_scope;
	
	sieve_dump_extension_set_context(dumper, &variables_extension, dctx);
	
	return TRUE;
}

/*
 * Variable identifier dump
 */

const char *ext_variables_dump_get_identifier
(const struct sieve_dumptime_env *denv, const struct sieve_extension *ext,
	unsigned int index)
{
	struct sieve_code_dumper *dumper = denv->cdumper;
	struct ext_variables_dump_context *dctx = sieve_dump_extension_get_context
		(dumper, &variables_extension);
	struct sieve_variable *var;

	if ( ext != NULL )
		return NULL;
			
	var = sieve_variable_scope_get_indexed(dctx->main_scope, index);
	
	return var->identifier;
}

