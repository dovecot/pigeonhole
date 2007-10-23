#include <stdio.h>
#include <string.h>

#include "lib.h"
#include "mempool.h"
#include "array.h"

#include "sieve-commands-private.h"
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-result.h"

#include "sieve-interpreter.h"

struct sieve_coded_stringlist {
  struct sieve_interpreter *interpreter;
  sieve_size_t start_address;
  sieve_size_t end_address;
  int length;
  int index;
};

#define CODE_AT_PC(interpreter) (interpreter->code[interpreter->pc])
#define DATA_AT_PC(interpreter) ((unsigned char) (interpreter->code[interpreter->pc]))
#define CODE_BYTES_LEFT(interpreter) (interpreter->code_size - interpreter->pc)
#define CODE_JUMP(interpreter, offset) interpreter->pc += offset

struct sieve_interpreter {
	pool_t pool;
	
	struct sieve_binary *binary;
	
	/* Direct pointer to code inside binary (which is considered immutable) */
	const char *code;
	sieve_size_t code_size;
	
	sieve_size_t pc; 
	
	struct sieve_result *result; 
};

struct sieve_interpreter *sieve_interpreter_create(struct sieve_binary *binary) 
{
	pool_t pool;
	struct sieve_interpreter *interpreter;
	
	pool = pool_alloconly_create("sieve_interpreter", 4096);	
	interpreter = p_new(pool, struct sieve_interpreter, 1);
	interpreter->pool = pool;
	
	interpreter->binary = binary;
	interpreter->code = sieve_binary_get_code(binary, &interpreter->code_size);	
	sieve_binary_ref(binary);
	
	interpreter->pc = 0;
	
	return interpreter;
}

void sieve_interpreter_free(struct sieve_interpreter *interpreter) 
{
	pool_unref(interpreter->pool);
	sieve_binary_unref(&interpreter->binary);
}

inline void sieve_interpreter_reset(struct sieve_interpreter *interpreter) 
{
	interpreter->pc = 0;
}

inline sieve_size_t sieve_interpreter_program_counter(struct sieve_interpreter *interpreter)
{
	return interpreter->pc;
}

/* Literals */

int sieve_interpreter_read_offset(struct sieve_interpreter *interpreter) 
{
	uint32_t offset = 0;
	
	if ( CODE_BYTES_LEFT(interpreter) >= 4 ) {
	  int i; 
	  
	  for ( i = 0; i < 4; i++ ) {
	    offset = (offset << 8) + DATA_AT_PC(interpreter);
	  	CODE_JUMP(interpreter, 1);
	  }
	}
	
	return (int) offset;
}

bool sieve_interpreter_read_integer
  (struct sieve_interpreter *interpreter, sieve_size_t *integer) 
{
  int bits = sizeof(sieve_size_t) * 8;
  *integer = 0;
  
  while ( (DATA_AT_PC(interpreter) & 0x80) > 0 ) {
    if ( CODE_BYTES_LEFT(interpreter) > 0 && bits > 0) {
      *integer |= DATA_AT_PC(interpreter) & 0x7F;
      CODE_JUMP(interpreter, 1);
    
      *integer <<= 7;
      bits -= 7;
    } else {
      /* This is an error */
      return FALSE;
    }
  }
  
  *integer |= DATA_AT_PC(interpreter) & 0x7F;
  CODE_JUMP(interpreter, 1);
  
  return TRUE;
}

/* FIXME: add this to lib/str. (cannot use str_c on non-modifyable buffer) */
static string_t *t_str_const(const void *cdata, size_t size)
{
	return buffer_create_const_data(pool_datastack_create(), cdata, size);
}

bool sieve_interpreter_read_string
  (struct sieve_interpreter *interpreter, string_t **str) 
{
  sieve_size_t strlen = 0;
  
  if ( !sieve_interpreter_read_integer(interpreter, &strlen) ) 
    return FALSE;
      
  if ( strlen > CODE_BYTES_LEFT(interpreter) ) 
    return FALSE;
   
  *str = t_str_const(&CODE_AT_PC(interpreter), strlen);
	CODE_JUMP(interpreter, strlen);
  
  return TRUE;
}

bool sieve_interpreter_read_stringlist
  (struct sieve_interpreter *interpreter, struct sieve_coded_stringlist **strlist)
{
  sieve_size_t pc = interpreter->pc;
  sieve_size_t end = pc + sieve_interpreter_read_offset(interpreter);
  sieve_size_t length = 0; 
  
  if ( !sieve_interpreter_read_integer(interpreter, &length) ) 
    return FALSE;
    
	*strlist = p_new(pool_datastack_create(), struct sieve_coded_stringlist, 1);
	(*strlist)->interpreter = interpreter;
	(*strlist)->start_address = pc;
	(*strlist)->end_address = end;
	(*strlist)->length = length;
	(*strlist)->index = 0;
  
  return TRUE;
}

bool sieve_coded_stringlist_read_item(struct sieve_coded_stringlist *strlist, string_t **str) 
{
  *str = NULL;
  
  if ( strlist->index >= strlist->length ) 
    return TRUE;
  else if ( sieve_interpreter_read_string(strlist->interpreter, str) ) {
    strlist->index++;
    return TRUE;
  }  
  
  return FALSE;
} 

/* Code Dump */

static void sieve_interpreter_dump_operation
	(struct sieve_interpreter *interpreter) 
{
	unsigned int opcode;
	
	if ( CODE_BYTES_LEFT(interpreter) > 0 ) {
		opcode = DATA_AT_PC(interpreter);
		
		printf("%08x: ", interpreter->pc);
		CODE_JUMP(interpreter, 1);
	
		if ( opcode < SIEVE_OPCODE_EXT_OFFSET ) {
			sieve_opcodes[opcode].dump(interpreter);
		} else {
		  const struct sieve_extension *ext = 
		  	sieve_binary_get_extension(interpreter->binary, opcode - SIEVE_OPCODE_EXT_OFFSET);
		  	
		  if ( ext->opcode.dump != NULL )
			  (void) ext->opcode.dump(interpreter);
			else
				printf("OPCODE: %d, Extension %s provides no opcode_dump implementation.\n", opcode, ext->name);
		}
	}		
}

void sieve_interpreter_dump_number
	(struct sieve_interpreter *interpreter) 
{
	sieve_size_t number = 0;
	
	if (sieve_interpreter_read_integer(interpreter, &number) ) {
	  printf("NUM: %ld\n", (long) number);		
	}
}

static void sieve_interpreter_print_string(string_t *str) 
{
	unsigned int i = 0;
  const unsigned char *sdata = str_data(str);

	printf("STR[%ld]: \"", (long) str_len(str));

	while ( i < 40 && i < str_len(str) ) {
	  if ( sdata[i] > 31 ) 
	    printf("%c", sdata[i]);
	  else
	    printf(".");
	    
	  i++;
	}
	
	if ( str_len(str) < 40 ) 
	  printf("\"\n");
	else
	  printf("...\n");
}

void sieve_interpreter_dump_string
	(struct sieve_interpreter *interpreter) 
{
	string_t *str; 
	
	if ( sieve_interpreter_read_string(interpreter, &str) ) {
		sieve_interpreter_print_string(str);   		
  }
}

void sieve_interpreter_dump_string_list
	(struct sieve_interpreter *interpreter) 
{
	struct sieve_coded_stringlist *strlist;
	
  if ( sieve_interpreter_read_stringlist(interpreter, &strlist) ) {
  	sieve_size_t pc = interpreter->pc;
		string_t *stritem;
		
		printf("STRLIST [%d] (END %08x)\n", strlist->length, strlist->end_address);
	  	
		while ( sieve_coded_stringlist_read_item(strlist, &stritem) && stritem != NULL ) {
			printf("%08x:      ", pc);
			sieve_interpreter_print_string(stritem);
			pc = interpreter->pc;  
		}
	}
}

void sieve_interpreter_dump_operand
	(struct sieve_interpreter *interpreter) 
{
  char opcode = CODE_AT_PC(interpreter);
	printf("%08x:   ", interpreter->pc);
	CODE_JUMP(interpreter, 1);
  
  if ( opcode < SIEVE_CORE_OPERAND_MASK ) {  	
    switch (opcode) {
    case SIEVE_OPERAND_NUMBER:
    	sieve_interpreter_dump_number(interpreter);
    	break;
    case SIEVE_OPERAND_STRING:
    	sieve_interpreter_dump_string(interpreter);
    	break;
    case SIEVE_OPERAND_STRING_LIST:
    	sieve_interpreter_dump_string_list(interpreter);
    	break;
    }
  }  
}

void sieve_interpreter_dump_code(struct sieve_interpreter *interpreter) 
{
	sieve_interpreter_reset(interpreter);
	
	while ( interpreter->pc < interpreter->code_size ) {
		sieve_interpreter_dump_operation(interpreter);
	}
	
	printf("%08x: [End of code]\n", interpreter->code_size);	
}

/* Code execute */

void sieve_interpreter_execute_opcode
	(struct sieve_interpreter *interpreter) 
{
	unsigned int opcode;
	
	if ( CODE_BYTES_LEFT(interpreter) > 0 ) {
		opcode = DATA_AT_PC(interpreter);
		CODE_JUMP(interpreter, 1);
	
		if ( opcode < SIEVE_OPCODE_EXT_OFFSET ) {
			sieve_opcodes[opcode].execute(interpreter);
		} else {
		  const struct sieve_extension *ext = 
		  	sieve_binary_get_extension(interpreter->binary, opcode - SIEVE_OPCODE_EXT_OFFSET);
		  	
		  printf("%08x: ", interpreter->pc-1);
		  if ( ext->opcode.execute != NULL )
			  (void) ext->opcode.execute(interpreter);
			else
				printf("OPCODE: %d, Extension %s provides no opcode_dump implementation.\n", opcode, ext->name);
		}
	}		
}

struct sieve_result *sieve_interpreter_run(struct sieve_interpreter *interpreter) 
{
	struct sieve_result *result;
	sieve_interpreter_reset(interpreter);
	
	result = sieve_result_create();
	
	while ( interpreter->pc < interpreter->code_size ) {
		sieve_interpreter_execute_opcode(interpreter);
	}
	
	return result;
}


