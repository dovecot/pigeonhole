#include "lib.h"

#include "sieve-commands.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-include-common.h"

/* Forward declarations */

static bool opc_return_execute
	(const struct sieve_opcode *opcode, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

static bool cmd_return_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED);
		
/* Return command 
 * 
 * Syntax
 *   return
 */	
const struct sieve_command cmd_return = { 
	"return", 
	SCT_COMMAND, 
	0, 0, FALSE, FALSE,
	NULL, NULL, NULL, 
	cmd_return_generate, 
	NULL
};

/* Return opcode */

const struct sieve_opcode return_opcode = { 
	"return",
	0,
	&include_extension,
	0,
	NULL, 
	opc_return_execute 
};

/*
 * Generation
 */

static bool cmd_return_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED) 
{
	return TRUE;
}

/*
 * Interpretation
 */

static bool opc_return_execute
(const struct sieve_opcode *opcode ATTR_UNUSED,
	const struct sieve_runtime_env *renv ATTR_UNUSED, 
	sieve_size_t *address ATTR_UNUSED)
{	
	return TRUE;
}


