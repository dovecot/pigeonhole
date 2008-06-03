#ifndef __EXT_VARIABLES_OPERANDS_H
#define __EXT_VARIABLES_OPERANDS_H

#include "lib.h"
#include "hash.h"
#include "str.h"
#include "array.h"

#include "sieve-common.h"
#include "ext-variables-common.h"

/* 
 * Variable operand 
 */
		
extern const struct sieve_operand variable_operand;	

void ext_variables_opr_variable_emit
	(struct sieve_binary *sbin, struct sieve_variable *var);

/* 
 * Match value operand 
 */
		
extern const struct sieve_operand match_value_operand;	

void ext_variables_opr_match_value_emit
	(struct sieve_binary *sbin, unsigned int index);

/* 
 * Variable string operand 
 */

extern const struct sieve_operand variable_string_operand;	

void ext_variables_opr_variable_string_emit
	(struct sieve_binary *sbin, unsigned int elements);
	
#endif /* __EXT_VARIABLES_OPERANDS_H */

