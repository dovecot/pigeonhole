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

struct sieve_interpreter {
	pool_t pool;
	
	struct sieve_binary *binary;
		
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
	sieve_binary_ref(binary);
	sieve_binary_commit(binary);
	
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
	
	if ( !sieve_interpreter_read_offset_operand(interpreter, &offset) )
		return FALSE;

	if ( pc + offset <= sieve_binary_get_code_size(interpreter->binary) && pc + offset > 0 ) {
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


/* Opcodes and operands */

static const struct sieve_opcode *sieve_interpreter_read_opcode
	(struct sieve_interpreter *interpreter) 
{
	unsigned int opcode;
	
	if ( sieve_binary_read_byte(interpreter->binary, &(interpreter->pc), &opcode) ) {
	
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
	return sieve_binary_read_offset(interpreter->binary, &(interpreter->pc), offset);
}

bool sieve_interpreter_read_number_operand
  (struct sieve_interpreter *interpreter, sieve_size_t *number)
{ 
	unsigned int op;
	
	if ( !sieve_binary_read_byte(interpreter->binary, &(interpreter->pc), &op) )
		return FALSE;
		
	if ( op != SIEVE_OPERAND_NUMBER ) 
		return FALSE;
	
	return sieve_binary_read_integer(interpreter->binary, &(interpreter->pc), number);
}

bool sieve_interpreter_read_string_operand
  (struct sieve_interpreter *interpreter, string_t **str)
{ 
	unsigned int op;
	
	if ( !sieve_binary_read_byte(interpreter->binary, &(interpreter->pc), &op) )
		return FALSE;
		
	if ( op != SIEVE_OPERAND_STRING ) 
		return FALSE;
	
	return sieve_binary_read_string(interpreter->binary, &(interpreter->pc), str);
}

struct sieve_coded_stringlist *
	sieve_interpreter_read_stringlist_operand
	  (struct sieve_interpreter *interpreter)
{
	unsigned int op;
	bool single = FALSE;
	
	if ( !sieve_binary_read_byte(interpreter->binary, &(interpreter->pc), &op) )
		return FALSE;
	
	switch ( op ) {
  case SIEVE_OPERAND_STRING:
  	single = TRUE;		
  	break;
 	case SIEVE_OPERAND_STRING_LIST:
 		single = FALSE;
 		break;
 	default:
  	return NULL;
	}
	
	return sieve_binary_read_stringlist
	  (interpreter->binary, &(interpreter->pc), single);
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
	
	if (sieve_binary_read_integer(interpreter->binary, &(interpreter->pc), &number) ) {
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
	
	if ( sieve_binary_read_string(interpreter->binary, &(interpreter->pc), &str) ) {
		sieve_interpreter_print_string(str);   		
		
		return TRUE;
  }
  
  return FALSE;
}

bool sieve_interpreter_dump_string_list
	(struct sieve_interpreter *interpreter) 
{
	struct sieve_coded_stringlist *strlist;
	
  if ( (strlist=sieve_binary_read_stringlist(interpreter->binary, &(interpreter->pc), FALSE)) != NULL ) {
  	sieve_size_t pc;
		string_t *stritem;
		
		printf("STRLIST [%d] (END %08x)\n", 
			sieve_coded_stringlist_get_length(strlist), 
			sieve_coded_stringlist_get_end_address(strlist));
	  	
	 	pc = sieve_coded_stringlist_get_current_offset(strlist);
		while ( sieve_coded_stringlist_next_item(strlist, &stritem) && stritem != NULL ) {	
			printf("%08x:      ", pc);
			sieve_interpreter_print_string(stritem);
			pc = sieve_coded_stringlist_get_current_offset(strlist);  
		}
		
		return TRUE;
	}
	
	return FALSE;
}

bool sieve_interpreter_dump_operand
	(struct sieve_interpreter *interpreter) 
{
  unsigned int op;
	printf("%08x:   ", interpreter->pc);
	
	if ( !sieve_binary_read_byte(interpreter->binary, &(interpreter->pc), &op) )
		return FALSE;
  
  if ( op < SIEVE_OPERAND_CORE_MASK ) {  	
    switch (op) {
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
	
	while ( interpreter->pc < sieve_binary_get_code_size(interpreter->binary) ) {
		if ( !sieve_interpreter_dump_operation(interpreter) ) {
			printf("Binary is corrupt.\n");
			return;
		}
	}
	
	printf("%08x: [End of code]\n", sieve_binary_get_code_size(interpreter->binary));	
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
	
	while ( interpreter->pc < sieve_binary_get_code_size(interpreter->binary) ) {
		printf("%08x: ", interpreter->pc);
		
		if ( !sieve_interpreter_execute_opcode(interpreter) ) {
			printf("Execution aborted.\n");
			return NULL;
		}
	}
	
	interpreter->result = NULL;
	
	return result;
}


