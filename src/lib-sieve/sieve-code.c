#include <stdio.h>

#include "sieve-code.h"
#include "sieve-interpreter.h"

/* Code dump for core commands */

void sieve_core_code_dump(struct sieve_interpreter *interpreter, sieve_size_t pc, int opcode)
{
	int offset;
	printf("%08x: ", pc);
	switch ( opcode ) {
	case SIEVE_OPCODE_LOAD:
		printf("LOAD\n");
		sieve_interpreter_dump_operand(interpreter);
		break;
	case SIEVE_OPCODE_JMP:
		offset = sieve_interpreter_read_offset(interpreter);
		printf("JMP %d [%08x]\n", offset, pc + 1 + offset);
		break;
	case SIEVE_OPCODE_JMPTRUE:
		offset = sieve_interpreter_read_offset(interpreter);
		printf("JMPTRUE %d [%08x]\n", offset, pc + 1 + offset);
		break;
	case SIEVE_OPCODE_JMPFALSE:
		offset = sieve_interpreter_read_offset(interpreter);
		printf("JMPFALSE %d [%08x]\n", offset, pc + 1 + offset);
		break;
	case SIEVE_OPCODE_STOP:
		printf("STOP\n");
		break;
	case SIEVE_OPCODE_KEEP:
		printf("KEEP\n");
		break;
	case SIEVE_OPCODE_DISCARD:
		printf("DISCARD\n");
		break;
	case SIEVE_OPCODE_ADDRESS:
		printf("ADDRESS\n");
		sieve_interpreter_dump_operand(interpreter);
		sieve_interpreter_dump_operand(interpreter);
		break;
	case SIEVE_OPCODE_HEADER:
		printf("HEADER\n");
		sieve_interpreter_dump_operand(interpreter);
		sieve_interpreter_dump_operand(interpreter);
		break;
	case SIEVE_OPCODE_EXISTS:
		printf("EXISTS\n");
		sieve_interpreter_dump_operand(interpreter);
		break;
	case SIEVE_OPCODE_SIZEOVER:
		printf("SIZEOVER\n");
		sieve_interpreter_dump_operand(interpreter);
		break;	
	case SIEVE_OPCODE_SIZEUNDER:
		printf("SIZEUNDER\n");
		sieve_interpreter_dump_operand(interpreter);
		break;	
	default:
		printf("??OPCODE?? [%d]\n", opcode);
	}
}

