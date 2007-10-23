#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h"
#include "sieve-code.h"

struct tst_size_context_data {
	enum { SIZE_UNASSIGNED, SIZE_UNDER, SIZE_OVER } type;
	unsigned int limit;
};

#define TST_SIZE_ERROR_DUP_TAG \
	"exactly one of the ':under' or ':over' tags must be specified for the size test, but more were found"

/* Tag validation */

static bool tst_size_validate_over_tag
	(struct sieve_validator *validator, 
	struct sieve_ast_argument **arg __attr_unused__, 
	struct sieve_command_context *tst)
{
	struct tst_size_context_data *ctx_data = (struct tst_size_context_data *) tst->data;	
	
	if ( ctx_data->type != SIZE_UNASSIGNED ) {
		sieve_command_validate_error(validator, tst, TST_SIZE_ERROR_DUP_TAG);
		return FALSE;		
	}
	
	ctx_data->type = SIZE_OVER;
	return TRUE;
}

static bool tst_size_validate_under_tag
	(struct sieve_validator *validator, 
	struct sieve_ast_argument **arg __attr_unused__, 
	struct sieve_command_context *tst)
{
	struct tst_size_context_data *ctx_data = (struct tst_size_context_data *) tst->data;	
	
	if ( ctx_data->type != SIZE_UNASSIGNED ) {
		sieve_command_validate_error(validator, tst, TST_SIZE_ERROR_DUP_TAG);
		return FALSE;		
	}
	
	ctx_data->type = SIZE_UNDER;	
	return TRUE;
}

/* Test registration */

static const struct sieve_tag size_over_tag = { "over", tst_size_validate_over_tag };
static const struct sieve_tag size_under_tag = { "under", tst_size_validate_under_tag };

bool tst_size_registered(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	sieve_validator_register_tag(validator, cmd_reg, &size_over_tag); 	
	sieve_validator_register_tag(validator, cmd_reg, &size_under_tag); 	

	return TRUE;
}

/* Test validation */

bool tst_size_validate(struct sieve_validator *validator, struct sieve_command_context *tst) 
{
	struct tst_size_context_data *ctx_data;
	struct sieve_ast_argument *arg;
	
	/* Assign context */
	ctx_data = p_new(sieve_command_pool(tst), struct tst_size_context_data, 1);
	ctx_data->type = SIZE_UNASSIGNED;
	ctx_data->limit = 0;
	tst->data = ctx_data;
	
	/* Check envelope test syntax:
	 *    size <":over" / ":under"> <limit: number>
	 */
	if ( !sieve_validate_command_arguments(validator, tst, 1, &arg) ||
		!sieve_validate_command_subtests(validator, tst, 0) ) 
		return FALSE;

	if ( ctx_data->type == SIZE_UNASSIGNED ) {
		sieve_command_validate_error(validator, tst, 
			"the size test requires either the :under or the :over tag to be specified");
		return FALSE;		
	}
		
	if ( sieve_ast_argument_type(arg) != SAAT_NUMBER ) {
		sieve_command_validate_error(validator, tst, 
			"the size test expects a number as argument (limit), but %s was found", 
			sieve_ast_argument_name(arg));
		return FALSE; 
	}
	
	ctx_data->limit = sieve_ast_argument_number(arg);
	
	return TRUE;
}

/* Test generation */

bool tst_size_generate
	(struct sieve_generator *generator, 
		struct sieve_command_context *ctx) 
{
	struct tst_size_context_data *ctx_data = (struct tst_size_context_data *) ctx->data;

	if ( ctx_data->type == SIZE_OVER ) 
		sieve_generator_emit_opcode(generator, SIEVE_OPCODE_SIZEOVER);
	else
		sieve_generator_emit_opcode(generator, SIEVE_OPCODE_SIZEUNDER);

	sieve_generator_emit_number(generator, ctx_data->limit);
	  
	return TRUE;
}

