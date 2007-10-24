#include <stdio.h>

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

/* Opcodes */

static bool tst_exists_opcode_dump(struct sieve_interpreter *interpreter);

const struct sieve_opcode tst_exists_opcode = 
	{ tst_exists_opcode_dump, NULL };

/* Test validation */

bool tst_exists_validate(struct sieve_validator *validator, struct sieve_command_context *tst) 
{
	struct sieve_ast_argument *arg;
	
	/* Check envelope test syntax:
	 *    exists <header-names: string-list>
	 */
	if ( !sieve_validate_command_arguments(validator, tst, 1, &arg) ||
		!sieve_validate_command_subtests(validator, tst, 0) ) { 
		return FALSE;
	}
		
	if ( sieve_ast_argument_type(arg) != SAAT_STRING && sieve_ast_argument_type(arg) != SAAT_STRING_LIST ) {
		sieve_command_validate_error(validator, tst, 
			"the exists test expects a string-list as only argument (header names), but %s was found", 
			sieve_ast_argument_name(arg));
		return FALSE; 
	}
	
	tst->data = arg;
	
	return TRUE;
}

/* Test generation */

bool tst_exists_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx) 
{
	struct sieve_ast_argument *arg = (struct sieve_ast_argument *) ctx->data;
	sieve_generator_emit_opcode(generator, SIEVE_OPCODE_EXISTS);
	
	/* Emit header names */
	if ( !sieve_generator_emit_stringlist_argument(generator, arg) ) 
		return FALSE;
	
	return TRUE;
}

/* Code dump */

static bool tst_exists_opcode_dump(struct sieve_interpreter *interpreter)
{
    printf("EXISTS\n");
    sieve_interpreter_dump_operand(interpreter);

    return TRUE;
}
