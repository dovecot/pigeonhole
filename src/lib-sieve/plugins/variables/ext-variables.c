/* Extension variables 
 * -------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5229
 * Implementation: basic variables support
 * Status: under development
 *
 */

#include "lib.h"
#include "str.h"
#include "unichar.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-binary.h"

#include "sieve-validator.h"

#include "ext-variables-common.h"

#include <ctype.h>

/* Forward declarations */

static bool ext_variables_load(int ext_id);
static bool ext_variables_validator_load(struct sieve_validator *validator);
static bool ext_variables_binary_load(struct sieve_binary *sbin);
static bool ext_variables_interpreter_load(struct sieve_interpreter *interp);

/* Commands */

extern const struct sieve_command cmd_set;
extern const struct sieve_command tst_string;

/* Operations */

extern const struct sieve_operation cmd_set_operation;
extern const struct sieve_operation tst_string_operation;

const struct sieve_operation *ext_variables_operations[] = {
	&cmd_set_operation, 
	&tst_string_operation
};

/* Operands */

extern const struct sieve_operand variable_operand;
extern const struct sieve_operand variable_string_operand;

const struct sieve_operand *ext_variables_operands[] = {
	&variable_operand, 
	&variable_string_operand
};

/* Extension definitions */

int ext_variables_my_id;
	
struct sieve_extension variables_extension = { 
	"variables", 
	ext_variables_load,
	ext_variables_validator_load, 
	NULL, 
	ext_variables_binary_load,
	ext_variables_interpreter_load, 
	SIEVE_EXT_DEFINE_OPERATIONS(ext_variables_operations), 
	SIEVE_EXT_DEFINE_OPERANDS(ext_variables_operands)
};

static bool ext_variables_load(int ext_id) 
{
	ext_variables_my_id = ext_id;
	return TRUE;
}

/* Load extension into validator */

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

/* Load extension intro binary */

static bool ext_variables_binary_load
	(struct sieve_binary *sbin)
{
	sieve_binary_registry_init(sbin, ext_variables_my_id);

	return TRUE;
}

/* Load extension into interpreter */

static bool ext_variables_interpreter_load
	(struct sieve_interpreter *interp)
{
	ext_variables_interpreter_initialize(interp);

	return TRUE;
}
