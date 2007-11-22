#include "lib.h"

#include "sieve-code.h"
#include "sieve-commands.h"
#include "sieve-validator.h" 
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-imapflags-common.h"

/* Forward declarations */

static bool cmd_removeflag_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx);

static bool cmd_removeflag_opcode_dump
	(const struct sieve_opcode *opcode ATTR_UNUSED,
		struct sieve_interpreter *interp ATTR_UNUSED, struct sieve_binary *sbin, 
		sieve_size_t *address);
static bool cmd_removeflag_opcode_execute
	(const struct sieve_opcode *opcode ATTR_UNUSED,
		struct sieve_interpreter *interp ATTR_UNUSED, struct sieve_binary *sbin, 
		sieve_size_t *address);

/* Removeflag command 
 *
 * Syntax:
 *   removeflag [<variablename: string>] <list-of-flags: string-list>
 */
 
const struct sieve_command cmd_removeflag = { 
	"removeflag", 
	SCT_COMMAND,
	-1, /* We check positional arguments ourselves */
	0, FALSE, FALSE, 
	NULL, NULL,
	ext_imapflags_command_validate, 
	cmd_removeflag_generate, 
	NULL 
};

/* Removeflag opcode */

const struct sieve_opcode removeflag_opcode = { 
	"REMOVEFLAG",
	SIEVE_OPCODE_CUSTOM,
	&imapflags_extension,
	EXT_IMAPFLAGS_OPCODE_REMOVEFLAG,
	cmd_removeflag_opcode_dump, 
	cmd_removeflag_opcode_execute 
};

/* 
 * Code generation 
 */

static bool cmd_removeflag_generate
	(struct sieve_generator *generator,	struct sieve_command_context *ctx)
{
	sieve_generator_emit_opcode_ext
		(generator, &removeflag_opcode, ext_imapflags_my_id);

	/* Generate arguments */
	if ( !sieve_generate_arguments(generator, ctx, NULL) )
		return FALSE;	

	return TRUE;
}

/* 
 * Code dump
 */
 
static bool cmd_removeflag_opcode_dump
(const struct sieve_opcode *opcode ATTR_UNUSED,
	struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin, sieve_size_t *address)
{
	printf("REMOVEFLAG\n");

	return 
		sieve_opr_string_dump(sbin, address);
}

/*
 * Execution
 */

static bool cmd_removeflag_opcode_execute
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

	printf(">> REMOVEFLAG \"%s\"\n", str_c(redirect));

	t_pop();
	return TRUE;
}
