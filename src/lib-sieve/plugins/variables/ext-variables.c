/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

/* Extension variables 
 * -------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5229
 * Implementation: mostly full; no support for future namespaces
 * Status: experimental, not thoroughly tested
 *
 */
 
/* FIXME: This implementation of the variables extension does not support 
 * namespaces. It recognizes them, but there is currently no support to let
 * an extension register a new namespace. Currently no such extension exists 
 * and therefore this support has a very low implementation priority.
 */

#include "lib.h"
#include "str.h"
#include "unichar.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"

#include "sieve-validator.h"

#include "ext-variables-common.h"
#include "ext-variables-arguments.h"
#include "ext-variables-operands.h"
#include "ext-variables-modifiers.h"

/* 
 * Operations 
 */

const struct sieve_operation *ext_variables_operations[] = {
	&cmd_set_operation, 
	&tst_string_operation
};

/* 
 * Operands 
 */

const struct sieve_operand *ext_variables_operands[] = {
	&variable_operand, 
	&match_value_operand,
	&variable_string_operand,
	&modifier_operand
};

/* 
 * Extension 
 */

static bool ext_variables_load(int ext_id);
static bool ext_variables_validator_load(struct sieve_validator *validator);

static int ext_my_id;
	
struct sieve_extension variables_extension = { 
	"variables", 
	&ext_my_id,
	ext_variables_load,
	ext_variables_validator_load, 
	ext_variables_generator_load,
	ext_variables_interpreter_load,
	NULL, NULL, 
	ext_variables_code_dump,
	SIEVE_EXT_DEFINE_OPERATIONS(ext_variables_operations), 
	SIEVE_EXT_DEFINE_OPERANDS(ext_variables_operands)
};

static bool ext_variables_load(int ext_id) 
{
	ext_my_id = ext_id;
	return TRUE;
}

static bool ext_variables_validator_load
	(struct sieve_validator *validator)
{
	sieve_validator_argument_override(validator, SAT_VAR_STRING, 
		&variable_string_argument); 
		
	sieve_validator_register_command(validator, &cmd_set);
	sieve_validator_register_command(validator, &tst_string);
	
	ext_variables_validator_initialize(validator);

	return TRUE;
}

