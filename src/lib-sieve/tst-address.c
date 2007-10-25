#include <stdio.h>

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

/* Opcodes */

static bool tst_address_opcode_dump(struct sieve_interpreter *interpreter);
static bool tst_address_opcode_execute(struct sieve_interpreter *interpreter);

const struct sieve_opcode tst_address_opcode = 
	{ tst_address_opcode_dump, tst_address_opcode_execute };

/* Test registration */

bool tst_address_registered(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	/* The order of these is not significant */
	sieve_validator_link_comparator_tag(validator, cmd_reg);
	sieve_validator_link_address_part_tags(validator, cmd_reg);
	sieve_validator_link_match_type_tags(validator, cmd_reg);

	return TRUE;
}

/* Test validation */

bool tst_address_validate(struct sieve_validator *validator, struct sieve_command_context *tst) 
{
	struct sieve_ast_argument *arg;
	
	/* Check envelope test syntax (optional tags are registered above):
	 *    address [ADDRESS-PART] [COMPARATOR] [MATCH-TYPE]
 	 *       <header-list: string-list> <key-list: string-list>
	 */
	if ( !sieve_validate_command_arguments(validator, tst, 2, &arg) ||
		!sieve_validate_command_subtests(validator, tst, 0) ) 
		return FALSE;
		
	tst->data = arg;
		
	if ( sieve_ast_argument_type(arg) != SAAT_STRING && sieve_ast_argument_type(arg) != SAAT_STRING_LIST ) {
		sieve_command_validate_error(validator, tst, 
			"the address test expects a string-list as first argument (header list), but %s was found", 
			sieve_ast_argument_name(arg));
		return FALSE; 
	}
	
	arg = sieve_ast_argument_next(arg);
	if ( sieve_ast_argument_type(arg) != SAAT_STRING && sieve_ast_argument_type(arg) != SAAT_STRING_LIST ) {
		sieve_command_validate_error(validator, tst, 
			"the address test expects a string-list as second argument (key list), but %s was found", 
			sieve_ast_argument_name(arg));
		return FALSE; 
	}
	
	return TRUE;
}

/* Test generation */

bool tst_address_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx ATTR_UNUSED) 
{
	struct sieve_ast_argument *arg = (struct sieve_ast_argument *) ctx->data;
	sieve_generator_emit_opcode(generator, SIEVE_OPCODE_ADDRESS);
	
	/* Emit header names */  	
	if ( !sieve_generator_emit_stringlist_argument(generator, arg) ) 
		return FALSE;
	
	/* Emit key list */
	arg = sieve_ast_argument_next(arg);
	if ( !sieve_generator_emit_stringlist_argument(generator, arg) ) 
		return FALSE;
	
	return TRUE;
}

/* Code dump */

static bool tst_address_opcode_dump(struct sieve_interpreter *interpreter)
{
	printf("ADDRESS\n");
	sieve_interpreter_dump_operand(interpreter);
	sieve_interpreter_dump_operand(interpreter);

	return TRUE;
}

/* Code execution */

static bool tst_address_opcode_execute(struct sieve_interpreter *interpreter)
{
	struct mail *mail = sieve_interpreter_get_mail(interpreter);
	struct sieve_coded_stringlist *hdr_list;
	struct sieve_coded_stringlist *key_list;
	string_t *hdr_item;
	bool matched;
	
	printf("?? ADDRESS\n");

	t_push();
		
	/* Read header-list */
	if ( (hdr_list=sieve_interpreter_read_stringlist_operand(interpreter)) == NULL ) {
		t_pop();
		return FALSE;
	}
	
	/* Read key-list */
	if ( (key_list=sieve_interpreter_read_stringlist_operand(interpreter)) == NULL ) {
		t_pop();
		return FALSE;
	}
	
	/* Iterate through all requested headers to match */
	hdr_item = NULL;
	matched = FALSE;
	while ( !matched && sieve_coded_stringlist_next_item(hdr_list, &hdr_item) && hdr_item != NULL ) {
		const char *const *headers;
			
		if ( mail_get_headers_utf8(mail, str_c(hdr_item), &headers) >= 0 ) {	
			
			int i;
			for ( i = 0; !matched && headers[i] != NULL; i++ ) {
				if ( sieve_stringlist_match(key_list, headers[i]) )
					matched = TRUE;				
			} 
		}
	}
	
	t_pop();
	
	sieve_interpreter_set_test_result(interpreter, matched);
	
	return TRUE;
}
