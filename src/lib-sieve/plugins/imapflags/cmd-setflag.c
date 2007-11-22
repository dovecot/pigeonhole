#include "lib.h"

#include "sieve-commands.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-imapflags-common.h"

/* Forward declarations */

static bool cmd_setflag_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx);

/* Setflag command 
 *
 * Syntax: 
 *   setflag [<variablename: string>] <list-of-flags: string-list>
 */
 
const struct sieve_command cmd_setflag = { 
	"setflag", 
	SCT_COMMAND,
	-1, /* We check positional arguments ourselves */
	0, FALSE, FALSE, 
	NULL, NULL,
	ext_imapflags_command_validate, 
	cmd_setflag_generate, 
	NULL 
};

/* Setflag opcode */

const struct sieve_opcode setflag_opcode = { 
	"SETFLAG",
	SIEVE_OPCODE_CUSTOM,
	&imapflags_extension,
	EXT_IMAPFLAGS_OPCODE_SETFLAG,
	NULL,
	NULL
};

/* Code generation */

static bool cmd_setflag_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx)
{
	sieve_generator_emit_opcode_ext
		(generator, &setflag_opcode, ext_imapflags_my_id);

	/* Generate arguments */
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;	

	return TRUE;
}


