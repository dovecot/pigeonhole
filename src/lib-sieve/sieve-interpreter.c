#include <stdio.h>
#include <string.h>

#include "lib.h"
#include "mempool.h"
#include "array.h"
#include "mail-storage.h"

#include "sieve-commands-private.h"
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-result.h"

#include "sieve-interpreter.h"

struct sieve_coded_stringlist {
  struct sieve_interpreter *interpreter;
  sieve_size_t start_address;
  sieve_size_t end_address;
  sieve_size_t current_offset;
  int length;
  int index;
};

#define CODE_AT_PC(interpreter) (interpreter->code[interpreter->pc])
#define DATA_AT_PC(interpreter) ((unsigned char) (interpreter->code[interpreter->pc]))
#define CODE_BYTES_LEFT(interpreter) (interpreter->code_size - interpreter->pc)
#define CODE_JUMP(interpreter, offset) interpreter->pc += offset

#define ADDR_CODE_AT(interpreter, address) (interpreter->code[*address])
#define ADDR_DATA_AT(interpreter, address) ((unsigned char) (interpreter->code[*address]))
#define ADDR_BYTES_LEFT(interpreter, address) (interpreter->code_size - (*address))
#define ADDR_JUMP(address, offset) (*address) += offset

struct sieve_interpreter {
	pool_t pool;
	
	struct sieve_binary *binary;
	
	/* Direct pointer to code inside binary (which is considered immutable) */
	const char *code;
	sieve_size_t code_size;
	
	/* Execution status */
	sieve_size_t pc; 
	bool test_result;
	struct sieve_result *result; 
	
	/* Execution environment */
	struct mail *mail;	
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
	sieve_binary_unref(&interpreter->binary);
	pool_unref(&(interpreter->pool));
}

/* Accessing runtinme environment */

inline struct mail *sieve_interpreter_get_mail(struct sieve_interpreter *interpreter) 
{
	return interpreter->mail;
}

/* Program counter */

inline void sieve_interpreter_reset(struct sieve_interpreter *interpreter) 
{
	interpreter->pc = 0;
}

inline sieve_size_t sieve_interpreter_program_counter(struct sieve_interpreter *interpreter)
{
	return interpreter->pc;
}

inline bool sieve_interpreter_program_jump
	(struct sieve_interpreter *interpreter, bool jump)
{
	sieve_size_t pc = sieve_interpreter_program_counter(interpreter);
	int offset;
	
	if ( !sieve_interpreter_read_offset(interpreter, &(interpreter->pc), &offset) )
		return FALSE;

	if ( pc + offset <= interpreter->code_size && pc + offset > 0 ) {
		if ( jump )
			interpreter->pc = pc + offset;
		
		return TRUE;
	}
	
	return FALSE;
}

inline void sieve_interpreter_set_test_result(struct sieve_interpreter *interpreter, bool result)
{
	interpreter->test_result = result;
}

inline bool sieve_interpreter_get_test_result(struct sieve_interpreter *interpreter)
{
	return interpreter->test_result;
}

/* Literals */

bool sieve_interpreter_read_offset
	(struct sieve_interpreter *interpreter, sieve_size_t *address, int *offset) 
{
	uint32_t offs = 0;
	
	if ( ADDR_BYTES_LEFT(interpreter, address) >= 4 ) {
	  int i; 
	  
	  for ( i = 0; i < 4; i++ ) {
	    offs = (offs << 8) + ADDR_DATA_AT(interpreter, address);
	  	ADDR_JUMP(address, 1);
	  }
	  
	  if ( offset != NULL )
			*offset = (int) offs;
			
		return TRUE;
	}
	
	return FALSE;
}

bool sieve_interpreter_read_integer
  (struct sieve_interpreter *interpreter, sieve_size_t *address, sieve_size_t *integer) 
{
  int bits = sizeof(sieve_size_t) * 8;
  *integer = 0;
  
  while ( (ADDR_DATA_AT(interpreter, address) & 0x80) > 0 ) {
    if ( ADDR_BYTES_LEFT(interpreter, address) > 0 && bits > 0) {
      *integer |= ADDR_DATA_AT(interpreter, address) & 0x7F;
      ADDR_JUMP(address, 1);
    
      *integer <<= 7;
      bits -= 7;
    } else {
      /* This is an error */
      return FALSE;
    }
  }
  
  *integer |= ADDR_DATA_AT(interpreter, address) & 0x7F;
  ADDR_JUMP(address, 1);
  
  return TRUE;
}

/* FIXME: add this to lib/str. */
static string_t *t_str_const(const void *cdata, size_t size)
{
	string_t *result = t_str_new(size);
	
	str_append_n(result, cdata, size);
	
	return result;
	//return buffer_create_const_data(pool_datastack_create(), cdata, size);
}

bool sieve_interpreter_read_string
  (struct sieve_interpreter *interpreter, sieve_size_t *address, string_t **str) 
{
  sieve_size_t strlen = 0;
  
  if ( !sieve_interpreter_read_integer(interpreter, address, &strlen) ) 
    return FALSE;
      
  if ( strlen > ADDR_BYTES_LEFT(interpreter, address) ) 
    return FALSE;
   
  *str = t_str_const(&ADDR_CODE_AT(interpreter, address), strlen);
	ADDR_JUMP(address, strlen);
  
  return TRUE;
}

struct sieve_coded_stringlist *sieve_interpreter_read_stringlist
  (struct sieve_interpreter *interpreter, sieve_size_t *address, bool single)
{
	struct sieve_coded_stringlist *strlist;

  sieve_size_t pc = *address;
  sieve_size_t end; 
  sieve_size_t length = 0; 
 
 	if ( single ) {
 		sieve_size_t strlen;
 		
 		if ( !sieve_interpreter_read_integer(interpreter, address, &strlen) ) 
    	return FALSE;
    	
    end = *address + strlen;
    length = 1;
    *address = pc;
	} else {
		int end_offset;
		
  	if ( !sieve_interpreter_read_offset(interpreter, address, &end_offset) )
  		return NULL;
  
  	end = pc + end_offset;
  
  	if ( !sieve_interpreter_read_integer(interpreter, address, &length) ) 
    	return NULL;
  }
  
  if ( end > interpreter->code_size ) 
  		return NULL;
    
	strlist = p_new(pool_datastack_create(), struct sieve_coded_stringlist, 1);
	strlist->interpreter = interpreter;
	strlist->start_address = *address;
	strlist->current_offset = *address;
	strlist->end_address = end;
	strlist->length = length;
	strlist->index = 0;
  
  /* Skip over the string list for now */
  *address = end;
  
  return strlist;
}

bool sieve_coded_stringlist_next_item(struct sieve_coded_stringlist *strlist, string_t **str) 
{
	sieve_size_t address;
  *str = NULL;
  
  if ( strlist->index >= strlist->length ) 
    return TRUE;
  else {
  	address = strlist->current_offset;
  	
  	if ( sieve_interpreter_read_string(strlist->interpreter, &address, str) ) {
    	strlist->index++;
    	strlist->current_offset = address;
    	return TRUE;
    }
  }  
  
  return FALSE;
}

void sieve_coded_stringlist_reset(struct sieve_coded_stringlist *strlist) 
{  
  strlist->current_offset = strlist->start_address;
  strlist->index = 0;
}

/* Opcodes and operands */

static const struct sieve_opcode *sieve_interpreter_read_opcode
	(struct sieve_interpreter *interpreter) 
{
	unsigned int opcode;
	
	if ( CODE_BYTES_LEFT(interpreter) > 0 ) {
		opcode = DATA_AT_PC(interpreter);
		CODE_JUMP(interpreter, 1);
	
		if ( opcode < SIEVE_OPCODE_EXT_OFFSET ) {
			if ( opcode < sieve_opcode_count )
				return sieve_opcodes[opcode];
			else
				return NULL;
		} else {
		  const struct sieve_extension *ext = 
		  	sieve_binary_get_extension(interpreter->binary, opcode - SIEVE_OPCODE_EXT_OFFSET);
		  
		  if ( ext != NULL )
		  	return &(ext->opcode);	
		  else
		  	return NULL;
		}
	}		
	
	return NULL;
}

bool sieve_interpreter_read_offset_operand
	(struct sieve_interpreter *interpreter, int *offset) 
{
	return sieve_interpreter_read_offset(interpreter, &(interpreter->pc), offset);
}

bool sieve_interpreter_read_number_operand
  (struct sieve_interpreter *interpreter, sieve_size_t *number)
{ 
	if ( CODE_AT_PC(interpreter) != SIEVE_OPERAND_NUMBER ) {
		return FALSE;
	}
	CODE_JUMP(interpreter, 1);

	return sieve_interpreter_read_integer(interpreter, &(interpreter->pc), number);
}

bool sieve_interpreter_read_string_operand
  (struct sieve_interpreter *interpreter, string_t **str)
{ 
	if ( CODE_AT_PC(interpreter) != SIEVE_OPERAND_STRING ) {
		return FALSE;
	}
	CODE_JUMP(interpreter, 1);

	return sieve_interpreter_read_string(interpreter, &(interpreter->pc), str);
}

struct sieve_coded_stringlist *
	sieve_interpreter_read_stringlist_operand
	  (struct sieve_interpreter *interpreter)
{
	bool single = FALSE;
	
	switch ( CODE_AT_PC(interpreter) ) {
  case SIEVE_OPERAND_STRING:
  	single = TRUE;		
  	break;
 	case SIEVE_OPERAND_STRING_LIST:
 		single = FALSE;
 		break;
 	default:
  	return NULL;
	}
	CODE_JUMP(interpreter, 1);
	
	return sieve_interpreter_read_stringlist
	  (interpreter, &(interpreter->pc), single);
}

/* Stringlist Utility */

bool sieve_stringlist_match
	(struct sieve_coded_stringlist *key_list, const char *value)
{
	string_t *key_item;
	sieve_coded_stringlist_reset(key_list);
				
	/* Match to all key values */
	key_item = NULL;
	while ( sieve_coded_stringlist_next_item(key_list, &key_item) && key_item != NULL ) {
		if ( strncmp(value, str_c(key_item), str_len(key_item)) == 0 )
			return TRUE;  
  }
  
  return FALSE;
}
 
/* Code Dump */

static bool sieve_interpreter_dump_operation
	(struct sieve_interpreter *interpreter) 
{
	const struct sieve_opcode *opcode = sieve_interpreter_read_opcode(interpreter);

	if ( opcode != NULL ) {
		printf("%08x: ", interpreter->pc-1);
	
		if ( opcode->dump != NULL )
			(void) opcode->dump(interpreter);
		else
			printf("<< UNSPECIFIED OPERATION >>\n");
			
		return TRUE;
	}		
	
	return FALSE;
}

bool sieve_interpreter_dump_number
	(struct sieve_interpreter *interpreter) 
{
	sieve_size_t number = 0;
	
	if (sieve_interpreter_read_integer(interpreter, &(interpreter->pc), &number) ) {
	  printf("NUM: %ld\n", (long) number);		
	  
	  return TRUE;
	}
	
	return FALSE;
}

void sieve_interpreter_print_string(string_t *str) 
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

bool sieve_interpreter_dump_string
	(struct sieve_interpreter *interpreter) 
{
	string_t *str; 
	
	if ( sieve_interpreter_read_string(interpreter, &(interpreter->pc), &str) ) {
		sieve_interpreter_print_string(str);   		
		
		return TRUE;
  }
  
  return FALSE;
}

bool sieve_interpreter_dump_string_list
	(struct sieve_interpreter *interpreter) 
{
	struct sieve_coded_stringlist *strlist;
	
  if ( (strlist=sieve_interpreter_read_stringlist(interpreter, &(interpreter->pc), FALSE)) != NULL ) {
  	sieve_size_t pc;
		string_t *stritem;
		
		printf("STRLIST [%d] (END %08x)\n", strlist->length, strlist->end_address);
	  	
	 	pc = strlist->current_offset;
		while ( sieve_coded_stringlist_next_item(strlist, &stritem) && stritem != NULL ) {	
			printf("%08x:      ", pc);
			sieve_interpreter_print_string(stritem);
			pc = strlist->current_offset;  
		}
		
		return TRUE;
	}
	
	return FALSE;
}

bool sieve_interpreter_dump_operand
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
    	
    default:
    	return FALSE;
    }
    
    return TRUE;
  }  
  
  return FALSE;
}

void sieve_interpreter_dump_code(struct sieve_interpreter *interpreter) 
{
	sieve_interpreter_reset(interpreter);
	
	interpreter->result = NULL;
	interpreter->mail = NULL;
	
	while ( interpreter->pc < interpreter->code_size ) {
		if ( !sieve_interpreter_dump_operation(interpreter) ) {
			printf("Binary is corrupt.\n");
			return;
		}
	}
	
	printf("%08x: [End of code]\n", interpreter->code_size);	
}

/* Code execute */

bool sieve_interpreter_execute_opcode
	(struct sieve_interpreter *interpreter) 
{
	const struct sieve_opcode *opcode = sieve_interpreter_read_opcode(interpreter);

	if ( opcode != NULL ) {
		if ( opcode->execute != NULL )
			return opcode->execute(interpreter);
		else
			printf("\n");
			
		return TRUE;
	}
	
	return FALSE;
}		

struct sieve_result *sieve_interpreter_run
	(struct sieve_interpreter *interpreter, struct mail *mail) 
{
	struct sieve_result *result;
	sieve_interpreter_reset(interpreter);
	
	result = sieve_result_create();
	interpreter->result = result;
	interpreter->mail = mail;
	
	while ( interpreter->pc < interpreter->code_size ) {
		printf("%08x: ", interpreter->pc);
		
		if ( !sieve_interpreter_execute_opcode(interpreter) ) {
			printf("Execution aborted.\n");
			return NULL;
		}
	}
	
	interpreter->result = NULL;
	
	return result;
}


