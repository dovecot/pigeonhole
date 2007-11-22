#include <stdio.h>

#include "lib.h"
#include "mempool.h"

#include "sieve-common.h"
#include "sieve-extensions.h"
#include "sieve-commands-private.h"
#include "sieve-code.h"
#include "sieve-binary.h"

#include "sieve-generator.h"

/* Jump list */
void sieve_jumplist_init(struct sieve_jumplist *jlist, struct sieve_binary *sbin)
{
	jlist->binary = sbin;
	t_array_init(&jlist->jumps, 4);
}

void sieve_jumplist_add(struct sieve_jumplist *jlist, sieve_size_t jump) 
{
	array_append(&jlist->jumps, &jump, 1);
}

void sieve_jumplist_resolve(struct sieve_jumplist *jlist) 
{
	unsigned int i;
	
	for ( i = 0; i < array_count(&jlist->jumps); i++ ) {
		const sieve_size_t *jump = array_idx(&jlist->jumps, i);
	
		sieve_binary_resolve_offset(jlist->binary, *jump);
	}
	
	array_free(&jlist->jumps);
}

/* Generator */

struct sieve_generator {
	pool_t pool;
	
	struct sieve_ast *ast;
	
	struct sieve_binary *binary;
};

struct sieve_generator *sieve_generator_create(struct sieve_ast *ast) 
{
	pool_t pool;
	struct sieve_generator *generator;
	
	pool = pool_alloconly_create("sieve_generator", 4096);	
	generator = p_new(pool, struct sieve_generator, 1);
	generator->pool = pool;
	
	generator->ast = ast;	
	sieve_ast_ref(ast);

	generator->binary = sieve_binary_create_new();
	sieve_binary_ref(generator->binary);
	
	return generator;
}

void sieve_generator_free(struct sieve_generator *generator) 
{
	sieve_ast_unref(&generator->ast);
	sieve_binary_unref(&generator->binary);
	pool_unref(&(generator->pool));
}

inline void sieve_generator_link_extension
	(struct sieve_generator *generator, int ext_id) 
{
	(void)sieve_binary_extension_link(generator->binary, ext_id);
}

/* Binary access */

inline struct sieve_binary *sieve_generator_get_binary
	(struct sieve_generator *gentr)
{
	return gentr->binary;
}

inline sieve_size_t sieve_generator_emit_opcode
	(struct sieve_generator *gentr, int opcode)
{
	return sieve_operation_emit_code(gentr->binary, opcode);
}

inline sieve_size_t sieve_generator_emit_opcode_ext
	(struct sieve_generator *gentr, int ext_id)
{	
	return sieve_operation_emit_code_ext(gentr->binary, ext_id);
}

/* Generator functions */

bool sieve_generate_arguments(struct sieve_generator *generator, 
	struct sieve_command_context *cmd, struct sieve_ast_argument **last_arg)
{
	enum { ARG_START, ARG_OPTIONAL, ARG_POSITIONAL } state = ARG_START;
	struct sieve_ast_argument *arg = sieve_ast_argument_first(cmd->ast_node);
	
	/* Parse all arguments with assigned generator function */
	
	while ( arg != NULL && arg->argument != NULL) {
		const struct sieve_argument *argument = arg->argument;
		
		switch ( state ) {
		case ARG_START: 
			if ( arg->arg_id_code == 0 )
				state = ARG_POSITIONAL;
			else {
				/* Mark start of optional operands with 0 operand identifier */
				sieve_binary_emit_byte(generator->binary, SIEVE_OPERAND_OPTIONAL);
				
				/* Emit argument id for optional operand */
				sieve_binary_emit_byte(generator->binary, arg->arg_id_code);

				state = ARG_OPTIONAL;
			}
			break;
		case ARG_OPTIONAL: 
			if ( arg->arg_id_code == 0 )
				state = ARG_POSITIONAL;
			
			/* Emit argument id for optional operand (0 marks the end of the optionals) */
			sieve_binary_emit_byte(generator->binary, arg->arg_id_code);

			break;
		case ARG_POSITIONAL:
			if ( arg->arg_id_code != 0 )
				return FALSE;
			break;
		}
		
		/* Call the generation function for the argument */ 
		if ( argument->generate != NULL ) { 
			if ( !argument->generate(generator, arg, cmd) ) 
				return FALSE;
		} else break;

		arg = sieve_ast_argument_next(arg);
	}
	
	if ( last_arg != NULL )
		*last_arg = arg;
	
	return TRUE;
}

bool sieve_generate_test
	(struct sieve_generator *generator, struct sieve_ast_node *tst_node,
		struct sieve_jumplist *jlist, bool jump_true) 
{
	i_assert( tst_node->context != NULL && tst_node->context->command != NULL );

	if ( tst_node->context->command->control_generate != NULL ) {
		if ( tst_node->context->command->control_generate
			(generator, tst_node->context, jlist, jump_true) ) 
			return TRUE;
		
		return FALSE;
	}
	
	if ( tst_node->context->command->generate != NULL ) {

		if ( tst_node->context->command->generate(generator, tst_node->context) ) {
			
			if ( jump_true ) 
				sieve_operation_emit_code(generator->binary, SIEVE_OPCODE_JMPTRUE);
			else
				sieve_operation_emit_code(generator->binary, SIEVE_OPCODE_JMPFALSE);
			sieve_jumplist_add(jlist, sieve_binary_emit_offset(generator->binary, 0));
						
			return TRUE;
		}	
		
		return FALSE;
	}
	
	return TRUE;
}

static bool sieve_generate_command
	(struct sieve_generator *generator, struct sieve_ast_node *cmd_node) 
{
	i_assert( cmd_node->context != NULL && cmd_node->context->command != NULL );

	if ( cmd_node->context->command->generate != NULL ) {
		return cmd_node->context->command->generate(generator, cmd_node->context);
	}
	
	return TRUE;		
}

bool sieve_generate_block
	(struct sieve_generator *generator, struct sieve_ast_node *block) 
{
	struct sieve_ast_node *command;

	t_push();	
	command = sieve_ast_command_first(block);
	while ( command != NULL ) {	
		sieve_generate_command(generator, command);	
		command = sieve_ast_command_next(command);
	}		
	t_pop();
	
	return TRUE;
}

struct sieve_binary *sieve_generator_run(struct sieve_generator *generator) {	
	if ( sieve_generate_block(generator, sieve_ast_root(generator->ast)) ) {
	 	return generator->binary;
	} 
	
	return NULL;
}


