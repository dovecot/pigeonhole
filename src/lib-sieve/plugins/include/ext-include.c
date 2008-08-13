/* Extension include
 * -----------------
 *
 * Authors: Stephan Bosch
 * Specification: draft-daboo-sieve-include-05
 * Implementation: full, but needs some more work 
 * Status: experimental, largely untested
 * 
 */
 
/* FIXME: Current include implementation does not allow for parts of the script
 * to be located in external binaries; all included scripts are recompiled and
 * the resulting byte code is imported into the main binary in separate blocks.
 */
 
#include "lib.h"

#include "sieve-common.h"

#include "sieve-extensions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-binary.h"
#include "sieve-dump.h"

#include "ext-include-common.h"
#include "ext-include-binary.h"

/* Forward declarations */

static bool ext_include_load(int ext_id);
static bool ext_include_validator_load(struct sieve_validator *validator);
static bool ext_include_generator_load(const struct sieve_codegen_env *cgenv);
static bool ext_include_binary_load(struct sieve_binary *sbin);
static bool ext_include_interpreter_load(struct sieve_interpreter *interp);

/* Operations */

static const struct sieve_operation *ext_include_operations[] = { 
	&include_operation, 
	&return_operation,
	&import_operation,
	&export_operation
};

/* Extension definitions */

static int ext_my_id;

const struct sieve_extension include_extension = { 
	"include", 
	&ext_my_id,
	ext_include_load,
	ext_include_validator_load, 
	ext_include_generator_load,
	ext_include_interpreter_load,
	ext_include_binary_load, 
	ext_include_binary_dump,
	SIEVE_EXT_DEFINE_OPERATIONS(ext_include_operations),
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

static bool ext_include_load(int ext_id)
{
	ext_my_id = ext_id;

	return TRUE;
}

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

static bool ext_include_generator_load(const struct sieve_codegen_env *cgenv)
{
	ext_include_register_generator_context(cgenv);

	return TRUE;
}

/* Load extension into interpreter */

static bool ext_include_interpreter_load
(struct sieve_interpreter *interp)
{
	ext_include_interpreter_context_init(interp);
	
	return TRUE;
}

/* Load extension into binary */

static bool ext_include_binary_load(struct sieve_binary *sbin)
{
	/* Register extension to the binary object to get notified of events like 
	 * opening or saving the binary. The implemententation of these hooks is found
	 * in ext-include-binary.c
	 */
	sieve_binary_extension_set(sbin, &include_extension, &include_binary_ext);
	
	return TRUE;
}
