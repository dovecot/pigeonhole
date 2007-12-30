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

/* Extension */

void ext_variables_validator_initialize(struct sieve_validator *validator);

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
};

const struct ext_variables_set_modifier *ext_variables_set_modifier_find
	(struct sieve_validator *validator, const char *identifier);
	
/* Variables */

void ext_variables_variable_emit
	(struct sieve_binary *sbin, struct sieve_variable *var);

void ext_variables_variable_argument_activate
	(struct sieve_validator *validator, struct sieve_ast_argument *arg);

struct sieve_variable *ext_variables_validator_get_variable
(struct sieve_validator *validator, const char *variable);

#endif /* __EXT_VARIABLES_COMMON_H */
