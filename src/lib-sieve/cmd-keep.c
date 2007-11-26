#include "lib.h"

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"

/* Forward declarations */

static bool opc_keep_execute
	(const struct sieve_opcode *opcode, 
		const struct sieve_runtime_env *renv, sieve_size_t *address);

static bool cmd_keep_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED);
		
/* Keep command 
 * 
 * Syntax
 *   keep
 */	
const struct sieve_command cmd_keep = { 
	"keep", 
	SCT_COMMAND, 
	0, 0, FALSE, FALSE,
	NULL, NULL, NULL, 
	cmd_keep_generate, 
	NULL
};

/* Keep opcode */

const struct sieve_opcode cmd_keep_opcode = { 
	"KEEP",
	SIEVE_OPCODE_KEEP,
	NULL,
	0,
	NULL, 
	opc_keep_execute 
};

/*
 * Generation
 */

static bool cmd_keep_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED) 
{
	sieve_operation_emit_code(
        sieve_generator_get_binary(generator), &cmd_keep_opcode);
	return TRUE;
}

/*
 * Interpretation
 */

static bool opc_keep_execute
(const struct sieve_opcode *opcode ATTR_UNUSED,
	const struct sieve_runtime_env *renv ATTR_UNUSED, 
	sieve_size_t *address ATTR_UNUSED)
{	
	printf(">> KEEP\n");
	
	sieve_act_store_add_to_result(renv, "INBOX");
	
	return TRUE;
}


