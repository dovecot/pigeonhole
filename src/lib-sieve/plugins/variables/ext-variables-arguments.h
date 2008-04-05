#ifndef __EXT_VARIABLES_ARGUMENTS_H
#define __EXT_VARIABLES_ARGUMENTS_H

#include "sieve-common.h"

/* 
 * Variable argument 
 */

extern const struct sieve_argument variable_argument;

bool ext_variables_variable_assignment_activate
	(struct sieve_validator *validator, struct sieve_ast_argument *arg,
		struct sieve_command_context *cmd);

/* 
 * Match value argument 
 */

extern const struct sieve_argument match_value_argument;

/* 
 * Variable string argument 
 */

extern const struct sieve_argument variable_string_argument;

#endif /* __EXT_VARIABLES_ARGUMENTS_H */
