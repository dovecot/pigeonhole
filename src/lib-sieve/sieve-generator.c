#include <stdio.h>

#include "lib.h"
#include "mempool.h"
#include "hash.h"

#include "sieve-common.h"
#include "sieve-extensions.h"
#include "sieve-commands-private.h"
#include "sieve-code.h"
#include "sieve-binary.h"

#include "sieve-generator.h"

/* Jump list */
void sieve_jumplist_init(struct sieve_jumplist *jlist)
{
	t_array_init(&jlist->jumps, 4);
}

void sieve_jumplist_add(struct sieve_jumplist *jlist, sieve_size_t jump) 
{
	array_append(&jlist->jumps, &jump, 1);
}

void sieve_jumplist_resolve(struct sieve_jumplist *jlist, struct sieve_generator *generator) 
{
	unsigned int i;
	
	for ( i = 0; i < array_count(&jlist->jumps); i++ ) {
		const sieve_size_t *jump = array_idx(&jlist->jumps, i);
	
		sieve_generator_resolve_offset(generator, *jump);
	}
	
	array_free(&jlist->jumps);
}

/* Extensions */

struct sieve_extension_registration {
	const struct sieve_extension *extension;
	unsigned int opcode;
};

/* Generator */

struct sieve_generator {
	pool_t pool;
	
	struct sieve_ast *ast;
	
	struct sieve_binary *binary;
	
	struct hash_table *extension_index; 
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
	
	generator->extension_index = hash_create
		(pool, pool, 0, NULL, NULL);
	
	return generator;
}

void sieve_generator_free(struct sieve_generator *generator) 
{
	hash_destroy(&generator->extension_index);
	
	sieve_ast_unref(&generator->ast);
	sieve_binary_unref(&generator->binary);
	pool_unref(&(generator->pool));
}

/* Registration functions */

void sieve_generator_register_extension
	(struct sieve_generator *generator, const struct sieve_extension *extension) 
{
	struct sieve_extension_registration *reg;
	
	reg = p_new(generator->pool, struct sieve_extension_registration, 1);
	reg->extension = extension;
	reg->opcode = sieve_binary_link_extension(generator->binary, extension);
	
	hash_insert(generator->extension_index, (void *) extension, (void *) reg);
}

unsigned int sieve_generator_find_extension		
	(struct sieve_generator *generator, const struct sieve_extension *extension) 
{
  struct sieve_extension_registration *reg = 
    (struct sieve_extension_registration *) hash_lookup(generator->extension_index, extension);
    
  return reg->opcode;
}

/* Emission functions */

sieve_size_t sieve_generator_emit_offset(struct sieve_generator *generator, int offset) 
{
  int i;
	sieve_size_t address = sieve_binary_get_code_size(generator->binary);

  for ( i = 3; i >= 0; i-- ) {
    char c = (char) (offset >> (i * 8));
	  (void) sieve_binary_emit_data(generator->binary, &c, 1);
	}
	
	return address;
}

void sieve_generator_resolve_offset
	(struct sieve_generator *generator, sieve_size_t address) 
{
  int i;
	int offset = sieve_binary_get_code_size(generator->binary) - address; 
	
	for ( i = 3; i >= 0; i-- ) {
    char c = (char) (offset >> (i * 8));
	  (void) sieve_binary_update_data(generator->binary, address + 3 - i, &c, 1);
	}
} 

/* Emit literals */

sieve_size_t sieve_generator_emit_integer(struct sieve_generator *generator, sieve_size_t integer)
{
  int i;
  char buffer[sizeof(sieve_size_t) + 1];
  int bufpos = sizeof(buffer) - 1;
  
  buffer[bufpos] = integer & 0x7F;
  bufpos--;
  integer >>= 7;
  while ( integer > 0 ) {
  	buffer[bufpos] = integer & 0x7F;
    bufpos--;
    integer >>= 7;  
  }
  
  bufpos++;
  if ( (sizeof(buffer) - bufpos) > 1 ) { 
    for ( i = bufpos; i < ((int) sizeof(buffer) - 1); i++) {
      buffer[i] |= 0x80;
    }
  } 
  
  return sieve_binary_emit_data(generator->binary, buffer + bufpos, sizeof(buffer) - bufpos);
}

sieve_size_t sieve_generator_emit_number(struct sieve_generator *generator, sieve_size_t number)
{
  sieve_size_t address = sieve_binary_emit_byte(generator->binary, SIEVE_OPERAND_NUMBER);
  
  (void) sieve_generator_emit_integer(generator, number);

  return address;
}

static sieve_size_t sieve_generator_emit_string_item(struct sieve_generator *generator, const string_t *str)
{
	sieve_size_t address = sieve_generator_emit_integer(generator, str_len(str));
  (void) sieve_binary_emit_data(generator->binary, (void *) str_data(str), str_len(str));

  return address;
}

sieve_size_t sieve_generator_emit_string(struct sieve_generator *generator, const string_t *str)
{
  sieve_size_t address = sieve_binary_emit_byte(generator->binary, SIEVE_OPERAND_STRING);
  
  (void) sieve_generator_emit_string_item(generator, str);
  
  return address;
}

bool sieve_generator_emit_stringlist_argument
	(struct sieve_generator *generator, struct sieve_ast_argument *arg)
{
	if ( sieve_ast_argument_type(arg) == SAAT_STRING ) {
		(void) sieve_generator_emit_string(generator, sieve_ast_argument_str(arg));
		return TRUE;
	} else if ( sieve_ast_argument_type(arg) == SAAT_STRING_LIST ) {
		if ( sieve_ast_strlist_count(arg) == 1 )
			sieve_generator_emit_string(generator, 
				sieve_ast_argument_str(sieve_ast_strlist_first(arg)));
		else
			(void) sieve_generator_emit_string_list(generator, arg);
		return TRUE;
	}
	
	return FALSE;
}

bool sieve_generator_emit_string_argument
	(struct sieve_generator *generator, struct sieve_ast_argument *arg)
{
	if ( sieve_ast_argument_type(arg) == SAAT_STRING ) {
		(void) sieve_generator_emit_string(generator, sieve_ast_argument_str(arg));
		return TRUE;
	} 
	
	return FALSE;
}

sieve_size_t sieve_generator_emit_string_list
	(struct sieve_generator *generator, const struct sieve_ast_argument *strlist)
{
	sieve_size_t address;
  const struct sieve_ast_argument *stritem;
  unsigned int listlen = sieve_ast_strlist_count(strlist);
  sieve_size_t end_offset = 0;

	/* Emit byte identifying the type of operand */	  
  address = sieve_binary_emit_byte(generator->binary, SIEVE_OPERAND_STRING_LIST);
  
  /* Give the interpreter an easy way to skip over this string list */
  end_offset = sieve_generator_emit_offset(generator, 0);

  /* Emit the length of the list */
  (void) sieve_generator_emit_integer(generator, (int) listlen);

	stritem = sieve_ast_strlist_first(strlist);
	while ( stritem != NULL ) {
		(void) sieve_generator_emit_string_item(generator, sieve_ast_strlist_str(stritem));
		
		stritem = sieve_ast_strlist_next(stritem);
	}

  (void) sieve_generator_resolve_offset(generator, end_offset);

  return address;
}

/* Emit commands */

sieve_size_t sieve_generator_emit_opcode
	(struct sieve_generator *generator, int opcode)
{
	unsigned char op = opcode & SIEVE_OPCODE_CORE_MASK;
	
	return sieve_binary_emit_byte(generator->binary, op);
}

sieve_size_t sieve_generator_emit_ext_opcode
	(struct sieve_generator *generator, const struct sieve_extension *extension)
{	
	unsigned char op = SIEVE_OPCODE_EXT_OFFSET + sieve_generator_find_extension(generator, extension);
	
	return sieve_binary_emit_byte(generator->binary, op);
}

/* Generator functions */

bool sieve_generate_arguments(struct sieve_generator *generator, 
	struct sieve_command_context *cmd, struct sieve_ast_argument **arg)
{
	/* Parse all arguments with assigned generator function */
	while ( *arg != NULL && (*arg)->argument != NULL) {
		const struct sieve_argument *argument = (*arg)->argument;
		
		/* Call the generation function for the argument */ 
		if ( argument->generate != NULL ) { 
			if ( !argument->generate(generator, arg, cmd) ) 
				return FALSE;
		} else break;
	}
	
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
				sieve_generator_emit_opcode(generator, SIEVE_OPCODE_JMPTRUE);
			else
				sieve_generator_emit_opcode(generator, SIEVE_OPCODE_JMPFALSE);
			sieve_jumplist_add(jlist, sieve_generator_emit_offset(generator, 0));
						
			return TRUE;
		}	
		
		return FALSE;
	}
	
	return TRUE;
}

static bool sieve_generate_command(struct sieve_generator *generator, struct sieve_ast_node *cmd_node) 
{
	i_assert( cmd_node->context != NULL && cmd_node->context->command != NULL );

	if ( cmd_node->context->command->generate != NULL ) {
		return cmd_node->context->command->generate(generator, cmd_node->context);
	}
	
	return TRUE;		
}

bool sieve_generate_block(struct sieve_generator *generator, struct sieve_ast_node *block) 
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


