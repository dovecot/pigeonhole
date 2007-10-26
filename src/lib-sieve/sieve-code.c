#include <stdio.h>

#include "sieve-code.h"
#include "sieve-interpreter.h"

/* Operands */



/* Opcodes */

static bool opc_jmp_dump(struct sieve_interpreter *interpreter);
static bool opc_jmptrue_dump(struct sieve_interpreter *interpreter);
static bool opc_jmpfalse_dump(struct sieve_interpreter *interpreter);
static bool opc_stop_dump(struct sieve_interpreter *interpreter);
static bool	opc_keep_dump(struct sieve_interpreter *interpreter);
static bool opc_discard_dump(struct sieve_interpreter *interpreter);

static bool opc_jmp_execute(struct sieve_interpreter *interpreter);
static bool opc_jmptrue_execute(struct sieve_interpreter *interpreter);
static bool opc_jmpfalse_execute(struct sieve_interpreter *interpreter);
static bool opc_stop_execute(struct sieve_interpreter *interpreter);
static bool opc_keep_execute(struct sieve_interpreter *interpreter);
static bool opc_discard_execute(struct sieve_interpreter *interpreter);

const struct sieve_opcode jmp_opcode = { opc_jmp_dump, opc_jmp_execute };
const struct sieve_opcode jmptrue_opcode = { opc_jmptrue_dump, opc_jmptrue_execute };
const struct sieve_opcode jmpfalse_opcode = { opc_jmpfalse_dump, opc_jmpfalse_execute };
const struct sieve_opcode stop_opcode = { opc_stop_dump, opc_stop_execute };
const struct sieve_opcode keep_opcode = { opc_keep_dump, opc_keep_execute };
const struct sieve_opcode discard_opcode = { opc_discard_dump, opc_discard_execute };

extern const struct sieve_opcode tst_address_opcode;
extern const struct sieve_opcode tst_header_opcode;
extern const struct sieve_opcode tst_exists_opcode;
extern const struct sieve_opcode tst_size_over_opcode;
extern const struct sieve_opcode tst_size_under_opcode;

const struct sieve_opcode *sieve_opcodes[] = {
  &jmp_opcode,
  &jmptrue_opcode, 
  &jmpfalse_opcode,
  &stop_opcode,
  &keep_opcode,
  &discard_opcode,

  &tst_address_opcode,
  &tst_header_opcode,
  &tst_exists_opcode,
  &tst_size_over_opcode,
  &tst_size_under_opcode
}; 

const unsigned int sieve_opcode_count =
	(sizeof(sieve_opcodes) / sizeof(sieve_opcodes[0]));

/* Code dump for core commands */

static bool opc_jmp_dump(struct sieve_interpreter *interpreter)
{
	unsigned int pc = sieve_interpreter_program_counter(interpreter);
	int offset;
	
	if ( sieve_interpreter_read_offset_operand(interpreter, &offset) ) 
		printf("JMP %d [%08x]\n", offset, pc + offset);
	else
		return FALSE;
	
	return TRUE;
}	
		
static bool opc_jmptrue_dump(struct sieve_interpreter *interpreter)
{	
	unsigned int pc = sieve_interpreter_program_counter(interpreter);
	int offset;
	
	if ( sieve_interpreter_read_offset_operand(interpreter, &offset) ) 
		printf("JMPTRUE %d [%08x]\n", offset, pc + offset);
	else
		return FALSE;
	
	return TRUE;
}

static bool opc_jmpfalse_dump(struct sieve_interpreter *interpreter)
{	
	unsigned int pc = sieve_interpreter_program_counter(interpreter);
	int offset;
	
	if ( sieve_interpreter_read_offset_operand(interpreter, &offset) )
		printf("JMPFALSE %d [%08x]\n", offset, pc + offset);
	else
		return FALSE;
	
	return TRUE;
}	
	
static bool opc_stop_dump(struct sieve_interpreter *interpreter ATTR_UNUSED)
{	
	printf("STOP\n");
	
	return TRUE;
}

static bool opc_keep_dump(struct sieve_interpreter *interpreter ATTR_UNUSED)
{	
	printf("KEEP\n");
	
	return TRUE;
}

static bool opc_discard_dump(struct sieve_interpreter *interpreter ATTR_UNUSED)
{	
	printf("DISCARD\n");
	
	return TRUE;
}

/* Code execution for core commands */

static bool opc_jmp_execute(struct sieve_interpreter *interpreter)
{
	printf("JMP\n");
	if ( !sieve_interpreter_program_jump(interpreter, TRUE) )
		return FALSE;
	
	return TRUE;
}	
		
static bool opc_jmptrue_execute(struct sieve_interpreter *interpreter)
{	
	if ( !sieve_interpreter_program_jump(interpreter,
		sieve_interpreter_get_test_result(interpreter)) )
		return FALSE;
		
	printf("JMPTRUE\n");
	
	return TRUE;
}

static bool opc_jmpfalse_execute(struct sieve_interpreter *interpreter)
{	
	if ( !sieve_interpreter_program_jump(interpreter,
		!sieve_interpreter_get_test_result(interpreter)) )
		return FALSE;
		
	printf("JMPFALSE\n");
	
	return TRUE;
}	
	
static bool opc_stop_execute(struct sieve_interpreter *interpreter ATTR_UNUSED)
{	
	printf(">> STOP\n");
	
	return FALSE;
}

static bool opc_keep_execute(struct sieve_interpreter *interpreter ATTR_UNUSED)
{	
	printf(">> KEEP\n");
	
	return TRUE;
}

static bool opc_discard_execute(struct sieve_interpreter *interpreter ATTR_UNUSED)
{	
	printf(">> DISCARD\n");
	
	return TRUE;
}

