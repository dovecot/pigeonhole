#ifndef __EXT_VARIABLES_COMMON_H
#define __EXT_VARIABLES_COMMON_H

#include "sieve-common.h"

#include "sieve-ext-variables.h"

extern int ext_variables_my_id;

extern struct sieve_extension variables_extension;

enum ext_variables_opcode {
	EXT_VARIABLES_OPERATION_SET,
	EXT_VARIABLES_OPERATION_STRING
};

enum ext_variables_operand {
	EXT_VARIABLES_OPERAND_VARIABLE,
	EXT_VARIABLES_OPERAND_VARIABLE_STRING
};

/* Extension */

void ext_variables_validator_initialize(struct sieve_validator *validator);
void ext_variables_interpreter_initialize(struct sieve_interpreter *interp);

/* Set modifiers */

enum ext_variables_set_modifier_code {
	EXT_VARIABLES_SET_MODIFIER_LOWER,
	EXT_VARIABLES_SET_MODIFIER_UPPER,
	EXT_VARIABLES_SET_MODIFIER_LOWERFIRST,
	EXT_VARIABLES_SET_MODIFIER_UPPERFIRST,
	EXT_VARIABLES_SET_MODIFIER_QUOTEWILDCARD,
	EXT_VARIABLES_SET_MODIFIER_LENGTH
};

struct ext_variables_set_modifier {
	const char *identifier;
	int code;
	
	unsigned int precedence;
	
	bool (*modify)(string_t *in, string_t *result);
};

const struct ext_variables_set_modifier *ext_variables_set_modifier_find
	(struct sieve_validator *validator, const char *identifier);
	
extern const struct ext_variables_set_modifier *core_modifiers[];
	
/* Arguments */

extern const struct sieve_argument variable_string_argument;
	
/* Variables */

void ext_variables_opr_variable_emit
	(struct sieve_binary *sbin, struct sieve_variable *var);
bool ext_variables_opr_variable_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address, 
		struct sieve_variable_storage **storage, unsigned int *var_index);

void ext_variables_opr_variable_string_emit
	(struct sieve_binary *sbin, unsigned int elements);

void ext_variables_variable_argument_activate
	(struct sieve_validator *validator, struct sieve_ast_argument *arg);

struct sieve_variable *ext_variables_validator_get_variable
(struct sieve_validator *validator, const char *variable);

struct sieve_variable_storage *ext_variables_interpreter_get_storage
	(struct sieve_interpreter *interp);
	
/* Extensions */

const struct sieve_variables_extension *
	sieve_variables_extension_get(struct sieve_binary *sbin, int ext_id);

#endif /* __EXT_VARIABLES_COMMON_H */
