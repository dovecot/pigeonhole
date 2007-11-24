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

static bool cmd_removeflag_opcode_execute
	(const struct sieve_opcode *opcode,	struct sieve_interpreter *interp, 
		struct sieve_binary *sbin, sieve_size_t *address);

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
	ext_imapflags_command_opcode_dump, 
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
 * Execution
 */

static bool cmd_removeflag_opcode_execute
(const struct sieve_opcode *opcode ATTR_UNUSED,
	struct sieve_interpreter *interp, 
	struct sieve_binary *sbin, sieve_size_t *address)
{
	string_t *flag_item;
	struct sieve_coded_stringlist *flag_list;
	
	printf("REMOVEFLAG\n");
	
	t_push();
		
	/* Read header-list */
	if ( (flag_list=sieve_opr_stringlist_read(sbin, address)) == NULL ) {
		t_pop();
		return FALSE;
	}
	
	/* Iterate through all requested headers to match */
	while ( sieve_coded_stringlist_next_item(flag_list, &flag_item) && 
		flag_item != NULL ) {
		ext_imapflags_remove_flags(interp, flag_item);
	}

	t_pop();
	
	printf("  FLAGS: %s\n", ext_imapflags_get_flags_string(interp));
	
	return TRUE;
}
