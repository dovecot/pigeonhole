/* Extension testsuite
 * -------------------
 *
 * Authors: Stephan Bosch
 * Specification: vendor-specific 
 *   (FIXME: provide specification for test authors)
 * Implementation: skeleton
 * Status: under development
 * Purpose: This custom extension is used to add sieve commands that act
 *   on the test suite. This provides the ability to specify and change the 
 *   input message inside the script and to fail validation, code generation and 
 *   execution based on predicates.
 */

#include <stdio.h>

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"

#include "testsuite-common.h"

/* Forward declarations */

static bool ext_testsuite_load(int ext_id);
static bool ext_testsuite_validator_load(struct sieve_validator *valdtr);
static bool ext_testsuite_generator_load(struct sieve_generator *gentr);
static bool ext_testsuite_binary_load(struct sieve_binary *sbin);

/* Commands */

extern const struct sieve_command cmd_test;
extern const struct sieve_command cmd_test_fail;
extern const struct sieve_command cmd_test_set;

/* Operations */

const struct sieve_operation *testsuite_operations[] = { 
	&test_operation, 
	&test_finish_operation,
	&test_fail_operation, 
	&test_set_operation 
};

/* Operands */

const struct sieve_operand *testsuite_operands[] =
    { &testsuite_object_operand };
    
/* Extension definitions */

int ext_testsuite_my_id;

const struct sieve_extension testsuite_extension = { 
	"vnd.dovecot.testsuite", 
	&ext_testsuite_my_id,
	ext_testsuite_load,
	ext_testsuite_validator_load,
	ext_testsuite_generator_load,
	NULL, NULL,
	ext_testsuite_binary_load, 
	NULL, 
	SIEVE_EXT_DEFINE_OPERATIONS(testsuite_operations),
	SIEVE_EXT_DEFINE_OPERAND(testsuite_object_operand)
};

static bool ext_testsuite_load(int ext_id)
{
	ext_testsuite_my_id = ext_id;

	return TRUE;
}

/* Load extension into validator */

static bool ext_testsuite_validator_load(struct sieve_validator *valdtr)
{
	sieve_validator_register_command(valdtr, &cmd_test);
	sieve_validator_register_command(valdtr, &cmd_test_fail);
	sieve_validator_register_command(valdtr, &cmd_test_set);
	
	return testsuite_validator_context_initialize(valdtr);
}

/* Load extension into generator */

static bool ext_testsuite_generator_load(struct sieve_generator *gentr)
{
	return testsuite_generator_context_initialize(gentr);
}

/* Load extension into binary */

static bool ext_testsuite_binary_load(struct sieve_binary *sbin ATTR_UNUSED)
{
	return TRUE;
}



