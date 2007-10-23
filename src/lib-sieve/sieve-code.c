#include <stdio.h>

#include "sieve-code.h"
#include "sieve-interpreter.h"

extern bool sieve_code_dump_jmp(struct sieve_interpreter *interpreter);
extern bool sieve_code_dump_jmptrue(struct sieve_interpreter *interpreter);
extern bool sieve_code_dump_jmpfalse(struct sieve_interpreter *interpreter);
extern bool sieve_code_dump_stop(struct sieve_interpreter *interpreter);
extern bool sieve_code_dump_keep(struct sieve_interpreter *interpreter);
extern bool sieve_code_dump_discard(struct sieve_interpreter *interpreter);
extern bool sieve_code_dump_address(struct sieve_interpreter *interpreter);
extern bool sieve_code_dump_header(struct sieve_interpreter *interpreter);
extern bool sieve_code_dump_exists(struct sieve_interpreter *interpreter);
extern bool sieve_code_dump_sizeover(struct sieve_interpreter *interpreter);
extern bool sieve_code_dump_sizeunder(struct sieve_interpreter *interpreter);

const struct sieve_opcode sieve_opcodes[] = {
  { sieve_code_dump_jmp, NULL },
  { sieve_code_dump_jmptrue, NULL },
  { sieve_code_dump_jmpfalse, NULL },
  { sieve_code_dump_stop, NULL },
  { sieve_code_dump_keep, NULL },
  { sieve_code_dump_discard, NULL },
  { sieve_code_dump_address, NULL },
  { sieve_code_dump_header, NULL },
  { sieve_code_dump_exists, NULL },
  { sieve_code_dump_sizeover, NULL },
  { sieve_code_dump_sizeunder, NULL }
}; 

/* Code dump for core commands */

bool sieve_code_dump_jmp(struct sieve_interpreter *interpreter)
{
	unsigned int pc = sieve_interpreter_program_counter(interpreter);
	int offset = sieve_interpreter_read_offset(interpreter);
	
	printf("JMP %d [%08x]\n", offset, pc + 1 + offset);
	
	return TRUE;
}	
		
bool sieve_code_dump_jmptrue(struct sieve_interpreter *interpreter)
{	
	unsigned int pc = sieve_interpreter_program_counter(interpreter);
	int offset = sieve_interpreter_read_offset(interpreter);
		
	printf("JMPTRUE %d [%08x]\n", offset, pc + 1 + offset);
	
	return TRUE;
}

bool sieve_code_dump_jmpfalse(struct sieve_interpreter *interpreter)
{	
	unsigned int pc = sieve_interpreter_program_counter(interpreter);
	int offset = sieve_interpreter_read_offset(interpreter);
	
	printf("JMPFALSE %d [%08x]\n", offset, pc + 1 + offset);
	
	return TRUE;
}	
	
bool sieve_code_dump_stop(struct sieve_interpreter *interpreter __attr_unused__)
{	
	printf("STOP\n");
	
	return TRUE;
}

bool sieve_code_dump_keep(struct sieve_interpreter *interpreter __attr_unused__)
{	
	printf("KEEP\n");
	
	return TRUE;
}

bool sieve_code_dump_discard(struct sieve_interpreter *interpreter __attr_unused__)
{	
	printf("DISCARD\n");
	
	return TRUE;
}

bool sieve_code_dump_address(struct sieve_interpreter *interpreter)
{	
	printf("ADDRESS\n");
	sieve_interpreter_dump_operand(interpreter);
	sieve_interpreter_dump_operand(interpreter);
	
	return TRUE;
}

bool sieve_code_dump_header(struct sieve_interpreter *interpreter)
{	
	printf("HEADER\n");
	sieve_interpreter_dump_operand(interpreter);
	sieve_interpreter_dump_operand(interpreter);
	
	return TRUE;
}

bool sieve_code_dump_exists(struct sieve_interpreter *interpreter)
{	
	printf("EXISTS\n");
	sieve_interpreter_dump_operand(interpreter);
	
	return TRUE;
}

bool sieve_code_dump_sizeover(struct sieve_interpreter *interpreter)
{	
	printf("SIZEOVER\n");
	sieve_interpreter_dump_operand(interpreter);
	
	return TRUE;
}

bool sieve_code_dump_sizeunder(struct sieve_interpreter *interpreter)
{	
	printf("SIZEUNDER\n");
	sieve_interpreter_dump_operand(interpreter);
	
	return TRUE;
}

