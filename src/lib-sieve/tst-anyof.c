#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h"
#include "sieve-code.h"
#include "sieve-binary.h"

/* Anyof test 
 *
 * Syntax 
 *   anyof <tests: test-list>   
 */

static bool tst_anyof_generate	
	(struct sieve_generator *generator, struct sieve_command_context *ctx,
		struct sieve_jumplist *jumps, bool jump_true);

const struct sieve_command tst_anyof = { 
	"anyof", 
	SCT_TEST, 
	0, 2, FALSE, FALSE,
	NULL, NULL, NULL, NULL, 
	tst_anyof_generate 
};

/* Code generation */

static bool tst_anyof_generate	
	(struct sieve_generator *generator, struct sieve_command_context *ctx,
		struct sieve_jumplist *jumps, bool jump_true)
{
	struct sieve_binary *sbin = sieve_generator_get_binary(generator);
	struct sieve_ast_node *test;
	struct sieve_jumplist true_jumps;
	
	if ( !jump_true ) {
		/* Prepare jumplist */
		sieve_jumplist_init(&true_jumps, sbin);
	}
	
	test = sieve_ast_test_first(ctx->ast_node);
	while ( test != NULL ) {	
		/* If this test list must jump on true, all sub-tests can simply add their jumps
		 * to the caller's jump list, otherwise this test redirects all true jumps to the 
		 * end of the currently generated code. This is just after a final jump to the false
		 * case 
		 */
		if ( !jump_true ) 
			sieve_generate_test(generator, test, &true_jumps, TRUE);
		else
			sieve_generate_test(generator, test, jumps, TRUE);
		
		test = sieve_ast_test_next(test);
	}	
	
	if ( !jump_true ) {
		/* All tests failed, jump to case FALSE */
		sieve_operation_emit_code(sbin, SIEVE_OPCODE_JMP);
		sieve_jumplist_add(jumps, sieve_binary_emit_offset(sbin, 0));
		
		/* All true exits jump here */
		sieve_jumplist_resolve(&true_jumps);
	}
		
	return TRUE;
}
