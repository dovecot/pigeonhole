/* Extension include
 * -----------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-daboo-sieve-include-05
 * Implementation: full, but needs much more work 
 * Status: experimental, largely untested
 * 
 */
 
/* FIXME: Current include implementation does not allow for parts of the script
 * to be located in external binaries; all included scripts are recompiled and
 * the resulting byte code is imported into the main binary in separate blocks.
 */
 
/* FIXME: As long as variables extension is not implemented, this extension will
 * not define import/export commands.
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
static bool ext_include_generator_load(struct sieve_generator *gentr);
static bool ext_include_binary_load(struct sieve_binary *sbin);
static bool ext_include_interpreter_load(struct sieve_interpreter *interp);

/* Operations */

extern const struct sieve_operation include_operation;
extern const struct sieve_operation return_operation;

static const struct sieve_operation *ext_include_operations[] = { 
	&include_operation, 
	&return_operation
};

/* Extension definitions */

int ext_include_my_id;

const struct sieve_extension include_extension = { 
	"include", 
	ext_include_load,
	ext_include_validator_load, 
	ext_include_generator_load,
	ext_include_binary_load, 
	ext_include_interpreter_load, 
	SIEVE_EXT_DEFINE_OPERATIONS(ext_include_operations),
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

static bool ext_include_load(int ext_id)
{
	ext_include_my_id = ext_id;

	return TRUE;
}

const struct sieve_binary_extension include_binary_ext = {
	&include_extension,
	ext_include_binary_save,
	ext_include_binary_open,
	ext_include_binary_free,
	ext_include_binary_up_to_date
};

/* Load extension into validator */

static bool ext_include_validator_load(struct sieve_validator *validator)
{
	/* Register new commands */
	sieve_validator_register_command(validator, &cmd_include);
	sieve_validator_register_command(validator, &cmd_return);
	sieve_validator_register_command(validator, &cmd_import);
	sieve_validator_register_command(validator, &cmd_export);

	return TRUE;
}	

/* Load extension into generator */

static bool ext_include_generator_load(struct sieve_generator *gentr)
{
	ext_include_register_generator_context(gentr);

	return TRUE;
}

/* Load extension into binary */

static bool ext_include_binary_load(struct sieve_binary *sbin)
{
	sieve_binary_extension_set(sbin, ext_include_my_id, &include_binary_ext);
	
	return TRUE;
}

/* Load extension into interpreter */

static bool ext_include_interpreter_load(struct sieve_interpreter *interp)
{
	ext_include_register_interpreter_context(interp);
	
	return TRUE;
}


