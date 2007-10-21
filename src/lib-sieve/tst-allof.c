#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h"

bool tst_allof_validate(struct sieve_validator *validator, struct sieve_command_context *tst) 
{
	/* Check envelope test syntax (optional tags are registered above):
	 *   allof <tests: test-list>   
	 */
	if ( !sieve_validate_command_arguments(validator, tst, 0, NULL) ||
		!sieve_validate_command_subtests(validator, tst, 2) ) 
		return FALSE;
		
	return TRUE;
}

bool tst_allof_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx,
		struct sieve_jumplist *jumps, bool jump_true)
{
	struct sieve_ast_node *test;
	struct sieve_jumplist false_jumps;
	
	if ( jump_true ) {
		/* Prepare jumplist */
		sieve_jumplist_init(&false_jumps);
	}
	
	test = sieve_ast_test_first(ctx->ast_node);
	while ( test != NULL ) {	
		/* If this test list must jump on false, all sub-tests can simply add their jumps
		 * to the caller's jump list, otherwise this test redirects all false jumps to the 
		 * end of the currently generated code. This is just after a final jump to the true
		 * case 
		 */
		if ( jump_true ) 
			sieve_generate_test(generator, test, &false_jumps, FALSE);
		else
			sieve_generate_test(generator, test, jumps, FALSE);
		
		test = sieve_ast_test_next(test);
	}	
	
	if ( jump_true ) {
		/* All tests succeeded, jump to case TRUE */
		sieve_generator_emit_core_opcode(generator, SIEVE_OPCODE_JMP);
		sieve_jumplist_add(jumps, sieve_generator_emit_offset(generator, 0));
		
		/* All false exits jump here */
		sieve_jumplist_resolve(&false_jumps, generator);
	}
		
	return TRUE;
}

