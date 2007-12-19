#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-code.h"
#include "sieve-binary.h"

/* Allof test 
 * 
 * Syntax 
 *   allof <tests: test-list>   
 */

static bool tst_allof_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx,
		struct sieve_jumplist *jumps, bool jump_true);

const struct sieve_command tst_allof = { 
	"allof", 
	SCT_TEST, 
	0, 2, FALSE, FALSE,
	NULL, NULL, NULL, NULL, 
	tst_allof_generate 
};

/* Code generation */

static bool tst_allof_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx,
		struct sieve_jumplist *jumps, bool jump_true)
{
	struct sieve_binary *sbin = sieve_generator_get_binary(generator);
	struct sieve_ast_node *test;
	struct sieve_jumplist false_jumps;
	
	if ( jump_true ) {
		/* Prepare jumplist */
		sieve_jumplist_init(&false_jumps, sbin);
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
		sieve_generator_emit_opcode(generator, &sieve_jmp_opcode);
		sieve_jumplist_add(jumps, sieve_binary_emit_offset(sbin, 0));
		
		/* All false exits jump here */
		sieve_jumplist_resolve(&false_jumps);
	}
		
	return TRUE;
}

