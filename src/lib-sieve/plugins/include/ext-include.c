/* Extension include
 * -----------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-daboo-sieve-include-05
 * Implementation: skeleton
 * Status: under development
 * 
 */

#include "lib.h"

#include "sieve-common.h"

#include "sieve-extensions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-binary.h"

#include "ext-include-common.h"

/* Forward declarations */

static bool ext_include_load(int ext_id);
static bool ext_include_validator_load(struct sieve_validator *validator);

/* Commands */

extern const struct sieve_command cmd_include;
extern const struct sieve_command cmd_return;

/* Opcodes */

extern const struct sieve_opcode include_opcode;
extern const struct sieve_opcode return_opcode;

static const struct sieve_opcode *ext_include_opcodes[] =
	{ &include_opcode, &return_opcode };

/* Extension definitions */

int ext_include_my_id;

const struct sieve_extension include_extension = { 
	"include", 
	ext_include_load,
	ext_include_validator_load, 
	NULL, NULL, NULL, 
	SIEVE_EXT_DEFINE_OPCODES(ext_include_opcodes),
	NULL
};

static bool ext_include_load(int ext_id)
{
	ext_include_my_id = ext_id;

	return TRUE;
}

/* Load extension into validator */

static bool ext_include_validator_load(struct sieve_validator *validator)
{
	/* Register new commands */
	sieve_validator_register_command(validator, &cmd_include);
	sieve_validator_register_command(validator, &cmd_return);
	
	ext_include_register_validator_context(validator,
		sieve_validator_get_script(validator));

	return TRUE;
}

