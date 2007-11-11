#include <stdio.h>
#include <string.h>

#include "lib.h"
#include "mempool.h"
#include "array.h"
#include "hash.h"
#include "mail-storage.h"

#include "sieve-commands-private.h"
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-result.h"

#include "sieve-interpreter.h"

struct sieve_interpreter {
	pool_t pool;
	
	struct sieve_binary *binary;
		
	/* Object registries */
	struct hash_table *registries; 
		
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
	struct sieve_interpreter *interp;
	
	pool = pool_alloconly_create("sieve_interpreter", 4096);	
	interp = p_new(pool, struct sieve_interpreter, 1);
	interp->pool = pool;
	
	interp->binary = binary;
	sieve_binary_ref(binary);
	sieve_binary_commit(binary);
	
	interp->pc = 0;

	interp->registries = hash_create(pool, pool, 0, NULL, NULL);
	
	/* Init core functionalities */
	sieve_comparators_init_registry(interp);
	
	return interp;
}

void sieve_interpreter_free(struct sieve_interpreter *interpreter) 
{
	sieve_binary_unref(&interpreter->binary);
	pool_unref(&(interpreter->pool));
}

/* Object registry */

struct sieve_interpreter_registry {
	struct sieve_interpreter *interpreter;
	const char *name;
	ARRAY_DEFINE(registered, void);
};

struct sieve_interpreter_registry *
	sieve_interpreter_registry_init(struct sieve_interpreter *interp, const char *name)
{
	struct sieve_interpreter_registry *reg = (struct sieve_interpreter_registry *) 
		hash_lookup(interp->registries, name);
	
	if ( reg == NULL ) {
		reg = p_new(interp->pool, struct sieve_interpreter_registry, 1);
		reg->interpreter = interp;
		reg->name = name;
		array_create(&reg->registered, interp->pool, sizeof(void *), 5);
		
		hash_insert(interp->registries, (void *) name, (void *) reg);
	}

	return reg;
}

const void *sieve_interpreter_registry_get
	(struct sieve_interpreter_registry *reg, const struct sieve_extension *ext)
{
	const void *result;
	int index = sieve_binary_get_extension_index(reg->interpreter->binary, ext);
	
	if  ( index < 0 || index > (int) array_count(&reg->registered) )
		return NULL;
	
	result = array_idx(&reg->registered, (unsigned int) index);		
	
	return result;
}

void  sieve_interpreter_registry_set
	(struct sieve_interpreter_registry *reg, const struct sieve_extension *ext, const void *obj)
{
	int index = sieve_binary_get_extension_index(reg->interpreter->binary, ext);
	
	if  ( index < 0 || index > (int) array_count(&reg->registered) )
		return;
	
	array_idx_set(&reg->registered, (unsigned int) index, obj);		
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

inline struct sieve_binary *sieve_interpreter_get_binary(struct sieve_interpreter *interp)
{
	return interp->binary;
}

/* Opcodes and operands */

bool sieve_interpreter_read_offset_operand
	(struct sieve_interpreter *interpreter, int *offset) 
{
	return sieve_binary_read_offset(interpreter->binary, &(interpreter->pc), offset);
}


/* Stringlist Utility */

bool sieve_stringlist_match
	(struct sieve_coded_stringlist *key_list, const char *value, const struct sieve_comparator *cmp)
{
	string_t *key_item;
	sieve_coded_stringlist_reset(key_list);
				
	/* Match to all key values */
	key_item = NULL;
	while ( sieve_coded_stringlist_next_item(key_list, &key_item) && key_item != NULL ) {
		if ( cmp->compare(value, strlen(value), str_c(key_item), str_len(key_item)) == 0 )
			return TRUE;  
  }
  
  return FALSE;
}
 
/* Code Dump */

static bool sieve_interpreter_dump_operation
	(struct sieve_interpreter *interp) 
{
	const struct sieve_opcode *opcode = 
		sieve_operation_read(interp->binary, &(interp->pc));

	if ( opcode != NULL ) {
		printf("%08x: ", interp->pc-1);
	
		if ( opcode->dump != NULL )
			return opcode->dump(interp, interp->binary, &(interp->pc));
		else
			printf("<< UNSPECIFIED OPERATION >>\n");
			
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

bool sieve_interpreter_execute_operation
	(struct sieve_interpreter *interp) 
{
	const struct sieve_opcode *opcode = sieve_operation_read(interp->binary, &(interp->pc));

	if ( opcode != NULL ) {
		if ( opcode->execute != NULL )
			return opcode->execute(interp, interp->binary, &(interp->pc));
		else
			printf("\n");
			
		return TRUE;
	}
	
	return FALSE;
}		

struct sieve_result *sieve_interpreter_run
	(struct sieve_interpreter *interp, struct mail *mail) 
{
	struct sieve_result *result;
	sieve_interpreter_reset(interp);
	
	result = sieve_result_create();
	interp->result = result;
	interp->mail = mail;
	
	while ( interp->pc < sieve_binary_get_code_size(interp->binary) ) {
		printf("%08x: ", interp->pc);
		
		if ( !sieve_interpreter_execute_operation(interp) ) {
			printf("Execution aborted.\n");
			return NULL;
		}
	}
	
	interp->result = NULL;
	
	return result;
}


