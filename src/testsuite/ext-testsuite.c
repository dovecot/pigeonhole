/* Extension testsuite
 * -------------------
 *
 * Authors: Stephan Bosch
 * Specification: vendor-specific 
 *   (FIXME: provide specification for test authors)
 * Implementation: skeleton
 * Status: under development
 * Purpose: This custom extension is used to add sieve commands that act
 *   on the test suite. This provides the ability to specify and change the input 
 *   message inside the script and to fail validation, code generation and 
 *   particularly execution based on predicates.
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

/* Forward declarations */

static bool ext_testsuite_load(int ext_id);
static bool ext_testsuite_validator_load(struct sieve_validator *validator);
static bool ext_testsuite_binary_load(struct sieve_binary *sbin);

/* Extension definitions */

int ext_testsuite_my_id;

extern const struct sieve_operation test_message_operation;

const struct sieve_operation *testsuite_operations[] =
    { &test_message_operation };

extern const struct sieve_command cmd_test_message;

const struct sieve_extension testsuite_extension = { 
	"vnd.dovecot.testsuite", 
	ext_testsuite_load,
	ext_testsuite_validator_load, 
	NULL, 
	ext_testsuite_binary_load, 
	NULL, 
	SIEVE_EXT_DEFINE_OPERATION(test_message_operation),
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

static bool ext_testsuite_load(int ext_id)
{
	ext_testsuite_my_id = ext_id;

	return TRUE;
}

/* Load extension into validator */
static bool ext_testsuite_validator_load(struct sieve_validator *validator)
{
	sieve_validator_register_command(validator, &cmd_test_message);
	return TRUE;
}

static bool ext_testsuite_binary_load(struct sieve_binary *sbin)
{
	return TRUE;
}



