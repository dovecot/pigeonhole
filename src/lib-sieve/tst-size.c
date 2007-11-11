#include <stdio.h>

#include "sieve-commands.h"
#include "sieve-commands-private.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code.h"

/* Opcodes */

static bool tst_size_over_opcode_dump
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);
static bool tst_size_under_opcode_dump
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);
static bool tst_size_over_opcode_execute
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);
static bool tst_size_under_opcode_execute
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address);

const struct sieve_opcode tst_size_over_opcode = 
	{ tst_size_over_opcode_dump, tst_size_over_opcode_execute };
const struct sieve_opcode tst_size_under_opcode = 
	{ tst_size_under_opcode_dump, tst_size_under_opcode_execute };

/* Context structures */

struct tst_size_context_data {
	enum { SIZE_UNASSIGNED, SIZE_UNDER, SIZE_OVER } type;
};

#define TST_SIZE_ERROR_DUP_TAG \
	"exactly one of the ':under' or ':over' tags must be specified for the size test, but more were found"

/* Tag validation */

static bool tst_size_validate_over_tag
	(struct sieve_validator *validator, 
	struct sieve_ast_argument **arg, 
	struct sieve_command_context *tst)
{
	struct tst_size_context_data *ctx_data = (struct tst_size_context_data *) tst->data;	
	
	if ( ctx_data->type != SIZE_UNASSIGNED ) {
		sieve_command_validate_error(validator, tst, TST_SIZE_ERROR_DUP_TAG);
		return FALSE;		
	}
	
	ctx_data->type = SIZE_OVER;
	
	/* Delete this tag */
	*arg = sieve_ast_arguments_delete(*arg, 1);
	
	return TRUE;
}

static bool tst_size_validate_under_tag
	(struct sieve_validator *validator, 
	struct sieve_ast_argument **arg ATTR_UNUSED, 
	struct sieve_command_context *tst)
{
	struct tst_size_context_data *ctx_data = (struct tst_size_context_data *) tst->data;	
	
	if ( ctx_data->type != SIZE_UNASSIGNED ) {
		sieve_command_validate_error(validator, tst, TST_SIZE_ERROR_DUP_TAG);
		return FALSE;		
	}
	
	ctx_data->type = SIZE_UNDER;
	
	/* Delete this tag */
	*arg = sieve_ast_arguments_delete(*arg, 1);
		
	return TRUE;
}

/* Test registration */

static const struct sieve_argument size_over_tag = 
	{ "over", tst_size_validate_over_tag, NULL };
static const struct sieve_argument size_under_tag = 
	{ "under", tst_size_validate_under_tag, NULL };

bool tst_size_registered(struct sieve_validator *validator, struct sieve_command_registration *cmd_reg) 
{
	sieve_validator_register_tag(validator, cmd_reg, &size_over_tag, 0); 	
	sieve_validator_register_tag(validator, cmd_reg, &size_under_tag, 0); 	

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
	sieve_validator_argument_activate(validator, arg);
	
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

 	/* Generate arguments */
    if ( !sieve_generate_arguments(generator, ctx, NULL) )
        return FALSE;
	  
	return TRUE;
}

/* Code dump */

static bool tst_size_over_opcode_dump
	(struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin, sieve_size_t *address)
{
    printf("SIZEOVER\n");
    
	return 
		sieve_opr_number_dump(sbin, address);
}

static bool tst_size_under_opcode_dump
	(struct sieve_interpreter *interp ATTR_UNUSED, 
	struct sieve_binary *sbin, sieve_size_t *address)
{ 
    printf("SIZEUNDER\n");

	return
    	sieve_opr_number_dump(sbin, address);
}

/* Code execution */

static bool tst_size_get(struct sieve_interpreter *interp, sieve_size_t *size) 
{
	struct mail *mail = sieve_interpreter_get_mail(interp);
	uoff_t psize;

	if ( mail_get_physical_size(mail, &psize) < 0 )
		return FALSE;

	*size = psize;
  
	return TRUE;
}

static bool tst_size_over_opcode_execute
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address)
{
	sieve_size_t mail_size, limit;
	
	printf("SIZEOVER\n");
	
	if ( !sieve_opr_number_read(sbin, address, &limit) ) 
		return FALSE;	
	
	if ( !tst_size_get(interp, &mail_size) )
		return FALSE;
	
	sieve_interpreter_set_test_result(interp, (mail_size > limit));
	
	return TRUE;
}

static bool tst_size_under_opcode_execute
	(struct sieve_interpreter *interp, struct sieve_binary *sbin, sieve_size_t *address)
{ 
	sieve_size_t mail_size, limit;
	
	printf("SIZEUNDER\n");
	
	if ( !sieve_opr_number_read(sbin, address, &limit) ) 
		return FALSE;	
	
	if ( !tst_size_get(interp, &mail_size) )
		return FALSE;
	
	sieve_interpreter_set_test_result(interp, (mail_size < limit));
	
	return TRUE;
}
