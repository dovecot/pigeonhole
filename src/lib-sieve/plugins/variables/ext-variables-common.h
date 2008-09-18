/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __EXT_VARIABLES_COMMON_H
#define __EXT_VARIABLES_COMMON_H

#include "sieve-common.h"
#include "sieve-validator.h"

#include "sieve-ext-variables.h"

/*
 * Extension
 */

extern struct sieve_extension variables_extension;

/* 
 * Commands 
 */

extern const struct sieve_command cmd_set;
extern const struct sieve_command tst_string;

/*
 * Operations
 */

extern const struct sieve_operation cmd_set_operation;
extern const struct sieve_operation tst_string_operation;

enum ext_variables_opcode {
	EXT_VARIABLES_OPERATION_SET,
	EXT_VARIABLES_OPERATION_STRING
};

/* 
 * Operands
 */


enum ext_variables_operand {
	EXT_VARIABLES_OPERAND_VARIABLE,
	EXT_VARIABLES_OPERAND_MATCH_VALUE,
	EXT_VARIABLES_OPERAND_VARIABLE_STRING,
	EXT_VARIABLES_OPERAND_MODIFIER
};

/* 
 * Validator context 
 */

struct ext_variables_validator_context {
	struct sieve_validator_object_registry *modifiers;
	
	struct sieve_variable_scope *main_scope;
};

void ext_variables_validator_initialize(struct sieve_validator *validator);
	
static inline struct ext_variables_validator_context *
ext_variables_validator_context_get(struct sieve_validator *valdtr)
{
	return (struct ext_variables_validator_context *)
		sieve_validator_extension_get_context(valdtr, &variables_extension);
}

struct sieve_variable *ext_variables_validator_get_variable
	(struct sieve_validator *validator, const char *variable, bool declare);

/*
 * Code generation
 */
 
bool ext_variables_generator_load
	(const struct sieve_codegen_env *cgenv);

/*
 * Interpreter context
 */	

bool ext_variables_interpreter_load
(const struct sieve_runtime_env *renv, sieve_size_t *address);

/* 
 * Variable coding 
 */

void ext_variables_opr_variable_emit
	(struct sieve_binary *sbin, struct sieve_variable *var);
void ext_variables_opr_match_value_emit
	(struct sieve_binary *sbin, unsigned int index);
bool ext_variables_opr_variable_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address, 
		struct sieve_variable_storage **storage, unsigned int *var_index);

void ext_variables_opr_variable_string_emit
	(struct sieve_binary *sbin, unsigned int elements);

bool ext_variables_variable_assignment_activate
(struct sieve_validator *validator, struct sieve_ast_argument *arg,
	struct sieve_command_context *cmd);
	
#endif /* __EXT_VARIABLES_COMMON_H */
