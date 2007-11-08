#include <stdio.h>

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

/* Opcodes */

static bool tst_exists_opcode_dump
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);
static bool tst_exists_opcode_execute
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);

const struct sieve_opcode tst_exists_opcode = 
	{ tst_exists_opcode_dump, tst_exists_opcode_execute };

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
	sieve_validator_argument_activate(validator, arg);
	
	tst->data = arg;
	
	return TRUE;
}

/* Test generation */

bool tst_exists_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx) 
{
	sieve_generator_emit_opcode(generator, SIEVE_OPCODE_EXISTS);

 	/* Generate arguments */
    if ( !sieve_generate_arguments(generator, ctx, NULL) )
        return FALSE;	
	
	return TRUE;
}

/* Code dump */

static bool tst_exists_opcode_dump
	(struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin, sieve_size_t *address)
{
    printf("EXISTS\n");

	return
    	sieve_opr_stringlist_dump(sbin, address);
}

/* Code execution */

static bool tst_exists_opcode_execute
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address)
{
	struct mail *mail = sieve_interpreter_get_mail(interp);
	struct sieve_coded_stringlist *hdr_list;
	string_t *hdr_item;
	bool matched;
	
	printf("?? EXISTS\n");

	t_push();
		
	/* Read header-list */
	if ( (hdr_list=sieve_opr_stringlist_read(sbin, address)) == NULL ) {
		t_pop();
		return FALSE;
	}
		
	/* Iterate through all requested headers to match */
	hdr_item = NULL;
	matched = FALSE;
	while ( !matched && sieve_coded_stringlist_next_item(hdr_list, &hdr_item) && hdr_item != NULL ) {
		const char *const *headers;
			
		if ( mail_get_headers_utf8(mail, str_c(hdr_item), &headers) >= 0 && headers[0] != NULL ) {	
			matched = TRUE;				 
		}
	}
	
	t_pop();
	
	sieve_interpreter_set_test_result(interp, matched);
	
	return TRUE;
}
