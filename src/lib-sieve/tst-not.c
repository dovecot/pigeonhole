#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h"
#include "sieve-generator.h"

/* Not test 
 *
 * Syntax:
 *   not <tests: test-list>   
 */

static bool tst_not_generate
	(struct sieve_generator *generator, struct sieve_command_context *ctx,
		struct sieve_jumplist *jumps, bool jump_true);

const struct sieve_command tst_not = { 
	"not", 
	SCT_TEST, 
	0, 1, FALSE, FALSE,
	NULL, NULL, NULL, NULL, 
	tst_not_generate 
};

/* Code generation */

static bool tst_not_generate
	(struct sieve_generator *generator, struct sieve_command_context *ctx,
		struct sieve_jumplist *jumps, bool jump_true)
{
	struct sieve_ast_node *test;
	
	/* Validator verified the existance of the single test already */
	test = sieve_ast_test_first(ctx->ast_node); 
	sieve_generate_test(generator, test, jumps, !jump_true);
		
	return TRUE;
}

