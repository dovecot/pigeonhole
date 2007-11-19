#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h"

bool tst_not_validate(struct sieve_validator *validator, struct sieve_command_context *tst) 
{
	/* Check not test syntax (optional tags are registered above):
	 *   allof <tests: test-list>   
	 */
	if ( !sieve_validate_command_arguments(validator, tst, 0) ||
		!sieve_validate_command_subtests(validator, tst, 1) ) 
		return FALSE;
	
	return TRUE;
}

bool tst_not_generate
	(struct sieve_generator *generator, struct sieve_command_context *ctx,
		struct sieve_jumplist *jumps, bool jump_true)
{
	struct sieve_ast_node *test;
	
	/* Validator verified the existance of the single test already */
	test = sieve_ast_test_first(ctx->ast_node); 
	sieve_generate_test(generator, test, jumps, !jump_true);
		
	return TRUE;
}

