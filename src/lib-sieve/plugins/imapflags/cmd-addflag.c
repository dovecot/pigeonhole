#include "lib.h"

#include "sieve-commands.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-imapflags-common.h"

/* Forward declarations */

static bool cmd_addflag_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx);
	
static bool cmd_addflag_opcode_execute
	(const struct sieve_opcode *opcode,	struct sieve_interpreter *interp, 
		struct sieve_binary *sbin, sieve_size_t *address);


/* Addflag command 
 *
 * Syntax:
 *   addflag [<variablename: string>] <list-of-flags: string-list>
 */
  
const struct sieve_command cmd_addflag = { 
	"addflag", 
	SCT_COMMAND,
	-1, /* We check positional arguments ourselves */
	0, FALSE, FALSE, 
	NULL, NULL,
	ext_imapflags_command_validate, 
	cmd_addflag_generate, 
	NULL 
};

/* Addflag opcode */

const struct sieve_opcode addflag_opcode = { 
	"ADDFLAG",
	SIEVE_OPCODE_CUSTOM,
	&imapflags_extension,
	EXT_IMAPFLAGS_OPCODE_ADDFLAG,
	sieve_opcode_string_dump,
	cmd_addflag_opcode_execute
};


/* Code generation */

static bool cmd_addflag_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx)
{
	sieve_generator_emit_opcode_ext	
		(generator, &addflag_opcode, ext_imapflags_my_id);

	/* Generate arguments */
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;	

	return TRUE;
}

/*
 * Execution
 */

static bool cmd_addflag_opcode_execute
(const struct sieve_opcode *opcode ATTR_UNUSED,
	struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin, sieve_size_t *address)
{
	string_t *redirect;

	t_push();

	if ( !sieve_opr_string_read(sbin, address, &redirect) ) {
		t_pop();
		return FALSE;
	}

	printf(">> ADDFLAG \"%s\"\n", str_c(redirect));

	t_pop();
	return TRUE;
}
