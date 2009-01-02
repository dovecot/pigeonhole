/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "hash.h"
#include "str.h"
#include "array.h"

#include "sieve-common.h"

#include "sieve-ast.h"
#include "sieve-binary.h"
#include "sieve-code.h"
#include "sieve-match-types.h"

#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-dump.h"
#include "sieve-interpreter.h"

#include "ext-variables-common.h"
#include "ext-variables-name.h"
#include "ext-variables-dump.h"

/* 
 * Variable operand 
 */

static bool opr_variable_read_value
	(const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str);
static bool opr_variable_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address, 
		const char *field_name);

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
	if ( var->ext == NULL ) {
		/* Default variable storage */
		(void) sieve_operand_emit_code(sbin, &variable_operand);
		(void) sieve_binary_emit_byte(sbin, 0);
		(void) sieve_binary_emit_unsigned(sbin, var->index);
		return;
	} 

	(void) sieve_operand_emit_code(sbin, &variable_operand);
	(void) sieve_binary_emit_extension(sbin, var->ext, 1);
	(void) sieve_binary_emit_unsigned(sbin, var->index);
}

static bool opr_variable_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address,
	const char *field_name) 
{
	unsigned int index = 0;
	const struct sieve_extension *ext;
	unsigned int code = 1; /* Initially set to offset value */
	const char *identifier;

	if ( !sieve_binary_read_extension(denv->sbin, address, &code, &ext) )
		return FALSE;
	
	if ( !sieve_binary_read_unsigned(denv->sbin, address, &index) )
		return FALSE;
		
	identifier = ext_variables_dump_get_identifier(denv, ext, index);
	identifier = identifier == NULL ? "??" : identifier;

	if ( ext == NULL ) {		
		if ( field_name != NULL ) 
			sieve_code_dumpf(denv, "%s: VAR ${%s} (%ld)", 
				field_name, identifier, (long) index);
		else
			sieve_code_dumpf(denv, "VAR ${%s} (%ld)", 
				identifier, (long) index);
	} else {
		if ( field_name != NULL ) 
			sieve_code_dumpf(denv, "%s: VAR [%s] ${%s} (%ld)", 
				field_name, ext->name, identifier, (long) index);
		else
			sieve_code_dumpf(denv, "VAR [%s] ${%s} (%ld)", 
				ext->name, identifier, (long) index);
	}
	return TRUE;
}

static bool opr_variable_read_value
(const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str)
{ 
	const struct sieve_extension *ext;
	unsigned int code = 1; /* Initially set to offset value */
	struct sieve_variable_storage *storage;
	unsigned int index = 0;
	
	if ( !sieve_binary_read_extension(renv->sbin, address, &code, &ext) )
		return FALSE;

	storage = sieve_ext_variables_get_storage(renv->interp, ext);
	if ( storage == NULL ) 
		return FALSE;
	
	if (sieve_binary_read_unsigned(renv->sbin, address, &index) ) {
		/* Parameter str can be NULL if we are requested to only skip and not 
		 * actually read the argument.
		 */
		if ( str != NULL ) {
			if ( !sieve_variable_get(storage, index, str) )
				return FALSE;
		
			if ( *str == NULL ) *str = t_str_new(0);
		}
		return TRUE;
	}
	
	return FALSE;
}
		
bool sieve_variable_operand_read_data
(const struct sieve_runtime_env *renv, const struct sieve_operand *operand, 
	sieve_size_t *address, struct sieve_variable_storage **storage, 
	unsigned int *var_index)
{
	const struct sieve_extension *ext;
	unsigned int code = 1; /* Initially set to offset value */
	unsigned int idx = 0;

	if ( operand != &variable_operand ) {
		return FALSE;
	}

	if ( !sieve_binary_read_extension(renv->sbin, address, &code, &ext) )
        return FALSE;
		
	*storage = sieve_ext_variables_get_storage(renv->interp, ext);
	if ( *storage == NULL )	
		return FALSE;
	
	if ( !sieve_binary_read_unsigned(renv->sbin, address, &idx) )
		return FALSE;		

	*var_index = idx;
	return TRUE;
}

bool sieve_variable_operand_read
(const struct sieve_runtime_env *renv, sieve_size_t *address, 
	struct sieve_variable_storage **storage, unsigned int *var_index)
{
	const struct sieve_operand *operand = sieve_operand_read(renv->sbin, address);

	return sieve_variable_operand_read_data
		(renv, operand, address, storage, var_index);
}
	
/* 
 * Match value operand 
 */

static bool opr_match_value_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str);
static bool opr_match_value_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address, 
		const char *field_name);

const struct sieve_opr_string_interface match_value_interface = { 
	opr_match_value_dump,
	opr_match_value_read
};
		
const struct sieve_operand match_value_operand = { 
	"match-value", 
	&variables_extension, 
	EXT_VARIABLES_OPERAND_MATCH_VALUE,
	&string_class,
	&match_value_interface
};	

void ext_variables_opr_match_value_emit
(struct sieve_binary *sbin, unsigned int index) 
{
	(void) sieve_operand_emit_code(sbin, &match_value_operand);
	(void) sieve_binary_emit_unsigned(sbin, index);
}

static bool opr_match_value_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address,
	const char *field_name) 
{
	unsigned int index = 0;
	
	if (sieve_binary_read_unsigned(denv->sbin, address, &index) ) {
		if ( field_name != NULL )
			sieve_code_dumpf(denv, "%s: MATCHVAL %lu", field_name, (unsigned long) index);
		else
			sieve_code_dumpf(denv, "MATCHVAL %lu", (unsigned long) index);

		return TRUE;
	}
	
	return FALSE;
}

static bool opr_match_value_read
(const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str)
{ 
	unsigned int index = 0;
			
	if (sieve_binary_read_unsigned(renv->sbin, address, &index) ) {
		/* Parameter str can be NULL if we are requested to only skip and not 
		 * actually read the argument.
		 	*/
		if ( str != NULL ) {
			sieve_match_values_get(renv->interp, index, str);
		
			if ( *str == NULL ) 
				*str = t_str_new(0);
			else if ( str_len(*str) > SIEVE_VARIABLES_MAX_VARIABLE_SIZE ) 
				str_truncate(*str, SIEVE_VARIABLES_MAX_VARIABLE_SIZE);
		}
		return TRUE;
	}
	
	return FALSE;
}
