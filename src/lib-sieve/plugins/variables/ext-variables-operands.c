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

/* 
 * Variable operand 
 */

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
	if ( var->ext == NULL ) {
		/* Default variable storage */
		(void) sieve_operand_emit_code(sbin, &variable_operand);
		(void) sieve_binary_emit_byte(sbin, 0);
		(void) sieve_binary_emit_integer(sbin, var->index);
		return;
	} 

	(void) sieve_operand_emit_code(sbin, &variable_operand);
	(void) sieve_binary_emit_extension(sbin, var->ext, 1);
	(void) sieve_binary_emit_integer(sbin, var->index);
}

static bool opr_variable_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address) 
{
	sieve_size_t index = 0;
	const struct sieve_extension *ext;
	unsigned int code = 1; /* Initially set to offset value */

	if ( !sieve_binary_read_extension(denv->sbin, address, &code, &ext) )
		return FALSE;
	
	if ( !sieve_binary_read_integer(denv->sbin, address, &index) )
		return FALSE;
		
	if ( ext == NULL )
		sieve_code_dumpf(denv, "VAR: %ld", (long) index);
	else
		sieve_code_dumpf(denv, "VAR: [%d:%s] %ld", *ext->id, ext->name, (long) index);
	return TRUE;
}

static bool opr_variable_read_value
  (const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str)
{ 
	const struct sieve_extension *ext;
	unsigned int code = 1; /* Initially set to offset value */
	struct sieve_variable_storage *storage;
	sieve_size_t index = 0;
	
	if ( !sieve_binary_read_extension(renv->sbin, address, &code, &ext) )
		return FALSE;

	storage = sieve_ext_variables_get_storage(renv->interp, ext, FALSE);
	if ( storage == NULL ) 
		return FALSE;
	
	if (sieve_binary_read_integer(renv->sbin, address, &index) ) {
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
	sieve_size_t idx = 0;

	if ( operand != &variable_operand ) {
		return FALSE;
	}

	if ( !sieve_binary_read_extension(renv->sbin, address, &code, &ext) )
        return FALSE;
		
	*storage = sieve_ext_variables_get_storage(renv->interp, ext, FALSE);
	if ( *storage == NULL )	
		return FALSE;
	
	if ( !sieve_binary_read_integer(renv->sbin, address, &idx) )
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
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);

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
	(void) sieve_binary_emit_integer(sbin, index);
}

static bool opr_match_value_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address) 
{
	sieve_size_t index = 0;
	
	if (sieve_binary_read_integer(denv->sbin, address, &index) ) {
		sieve_code_dumpf(denv, "MVALUE: %ld", (long) index);

		return TRUE;
	}
	
	return FALSE;
}

static bool opr_match_value_read
  (const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str)
{ 
	sieve_size_t index = 0;
			
	if (sieve_binary_read_integer(renv->sbin, address, &index) ) {
		/* Parameter str can be NULL if we are requested to only skip and not 
		 * actually read the argument.
		 	*/
		if ( str != NULL ) {
			sieve_match_values_get(renv->interp, (unsigned int) index, str);
		
			if ( *str == NULL ) 
				*str = t_str_new(0);
			else if ( str_len(*str) > SIEVE_VARIABLES_MAX_VARIABLE_SIZE ) 
				str_truncate(*str, SIEVE_VARIABLES_MAX_VARIABLE_SIZE);
		}
		return TRUE;
	}
	
	return FALSE;
}

/* 
 * Variable string operand 
 */

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
	(void) sieve_operand_emit_code(sbin, &variable_string_operand);
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

	/* Parameter str can be NULL if we are requested to only skip and not 
	 * actually read the argument.
	 */
	if ( str == NULL ) {
		for ( i = 0; i < (unsigned int) elements; i++ ) {		
			if ( !sieve_opr_string_read(renv, address, NULL) ) 
				return FALSE;
		}
	} else {
		string_t *strelm;
		string_t **elm = &strelm;

		*str = t_str_new(128);
		for ( i = 0; i < (unsigned int) elements; i++ ) {
		
			if ( !sieve_opr_string_read(renv, address, elm) ) 
				return FALSE;
		
			if ( elm != NULL ) {
				str_append_str(*str, strelm);

				if ( str_len(*str) > SIEVE_VARIABLES_MAX_VARIABLE_SIZE ) {
					str_truncate(*str, SIEVE_VARIABLES_MAX_VARIABLE_SIZE);
					elm = NULL;
				}
			}
		}
	}

	return TRUE;
}
