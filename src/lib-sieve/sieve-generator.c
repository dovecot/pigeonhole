#include <stdio.h>

#include "lib.h"
#include "mempool.h"
#include "buffer.h"
#include "array.h"
#include "hash.h"

#include "sieve-common.h"
#include "sieve-commands-private.h"
#include "sieve-generator.h"

/* Jump list */
void sieve_jumplist_init(struct sieve_jumplist *jlist)
{
	ARRAY_CREATE(&jlist->jumps, pool_datastack_create(), sieve_size_t, 4);
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

/* Generator */

struct sieve_generator {
	pool_t pool;
	
	struct sieve_ast *ast;
	
	buffer_t *code_buffer;
	struct hash_table *opcodes; 
};

struct sieve_generator *sieve_generator_create(struct sieve_ast *ast) {
	pool_t pool;
	struct sieve_generator *generator;
	
	pool = pool_alloconly_create("sieve_generator", 4096);	
	generator = p_new(pool, struct sieve_generator, 1);
	generator->pool = pool;
	
	generator->ast = ast;	
	sieve_ast_ref(ast);
	
	generator->code_buffer = buffer_create_dynamic(pool, 256);
	
	generator->opcodes = hash_create
		(pool, pool, 0, NULL, NULL);
	
	return generator;
}

void sieve_generator_free(struct sieve_generator *generator) 
{
	hash_destroy(generator->opcodes);
	
	sieve_ast_unref(&generator->ast);
	pool_unref(generator->pool);
}

/* Registration functions */

void sieve_generator_register_extension
	(struct sieve_validator *generator, const struct sieve_extension *extension) 
{
	unsigned int index = hash_size(generator->extensions);
	 
	hash_insert(generator->extension, (void *) extension, (void *) index);
}

unsigned int sieve_generator_find_opcode
		(struct sieve_validator *generator, const struct sieve_opcode *opcode) 
{
  return 	(unsigned int) hash_lookup(generator->opcodes, opcode);
}

/* Emission functions */

inline sieve_size_t sieve_generator_emit_data(struct sieve_generator *generator, void *data, sieve_size_t size) 
{
	buffer_append(generator->code_buffer, data, size);
	
	return buffer_get_used_size(generator->code_buffer);
}

inline sieve_size_t sieve_generator_emit_byte(struct sieve_generator *generator, unsigned char byte) 
{
  sieve_size_t address = buffer_get_used_size(generator->code_buffer);
  
	buffer_append(generator->code_buffer, &byte, 1);
	
	return address;
}

inline void sieve_generator_update_data
	(struct sieve_generator *generator, sieve_size_t address, void *data, sieve_size_t size) 
{
	buffer_write(generator->code_buffer, address, data, size);
}

inline sieve_size_t sieve_generator_get_current_address(struct sieve_generator *generator)
{
	return buffer_get_used_size(generator->code_buffer);
}

/* 
 */
sieve_size_t sieve_generator_emit_offset(struct sieve_generator *generator, int offset) 
{
  int i;
	sieve_size_t address = buffer_get_used_size(generator->code_buffer);

  for ( i = 3; i >= 0; i-- ) {
    char c = (char) (offset >> (i * 8));
	  (void) sieve_generator_emit_data(generator, &c, 1);
	}
	
	return address;
}

void sieve_generator_resolve_offset
	(struct sieve_generator *generator, sieve_size_t address) 
{
  int i;
	int offset = sieve_generator_get_current_address(generator) - address; 
	
	for ( i = 3; i >= 0; i-- ) {
    char c = (char) (offset >> (i * 8));
	  (void) sieve_generator_update_data(generator, address + 3 - i, &c, 1);
	}
} 

/* Emit literals */

sieve_size_t sieve_generator_emit_integer(struct sieve_generator *generator, sieve_size_t integer)
{
  int i;
	sieve_size_t address = buffer_get_used_size(generator->code_buffer);
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
  
  (void) sieve_generator_emit_data(generator, buffer + bufpos, sizeof(buffer) - bufpos);
  
  return address;
}

sieve_size_t sieve_generator_emit_number(struct sieve_generator *generator, sieve_size_t number)
{
  sieve_size_t address = sieve_generator_emit_byte(generator, SIEVE_OPERAND_NUMBER);
  
  (void) sieve_generator_emit_integer(generator, number);

  return address;
}

static sieve_size_t sieve_generator_emit_string_item(struct sieve_generator *generator, const string_t *str)
{
	sieve_size_t address = buffer_get_used_size(generator->code_buffer);
  
  (void) sieve_generator_emit_integer(generator, str_len(str));
  (void) sieve_generator_emit_data(generator, (void *) str_data(str), str_len(str));

  return address;
}

sieve_size_t sieve_generator_emit_string(struct sieve_generator *generator, const string_t *str)
{
  sieve_size_t address = sieve_generator_emit_byte(generator, SIEVE_OPERAND_STRING);
  
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
  const struct sieve_ast_argument *stritem;
  unsigned int listlen = sieve_ast_strlist_count(strlist);
  sieve_size_t end_offset = 0;
  
  sieve_size_t address = sieve_generator_emit_byte(generator, SIEVE_OPERAND_STRING_LIST);
  
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

sieve_size_t sieve_generator_emit_core_opcode(struct sieve_generator *generator, int opcode) 
{
	unsigned char op = opcode & 0x3F;
	
	return sieve_generator_emit_data(generator, (void *) &op, 1);
}

sieve_size_t sieve_generator_emit_opcode(struct sieve_generator *generator, struct sieve_opcode *opcode) 
{
	unsigned char op = (1 << 6) + sieve_generator_find_opcode(generator, opcode);
	
	return sieve_generator_emit_data(generator, (void *) &op, 1);
}

/* Generator functions */

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
				sieve_generator_emit_core_opcode(generator, NULL, SIEVE_OPCODE_JMPTRUE);
			else
				sieve_generator_emit_core_opcode(generator, NULL, SIEVE_OPCODE_JMPFALSE);
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

bool sieve_generate(struct sieve_generator *generator, buffer_t **code) {	
	if ( sieve_generate_block(generator, sieve_ast_root(generator->ast)) ) {
		if ( code != NULL )
			*code = generator->code_buffer;
	 	return TRUE;
	} 
	
	return FALSE;
}


