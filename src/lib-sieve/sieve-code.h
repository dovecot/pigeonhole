#ifndef __SIEVE_CODE_H__
#define __SIEVE_CODE_H__

#include <lib.h>
#include <buffer.h>
#include <array.h>

#include "sieve-common.h"
#include "sieve-extensions.h"

typedef size_t sieve_size_t;

enum sieve_core_operation {
	SIEVE_OPCODE_LOAD      = 0x01,
	SIEVE_OPCODE_JMP       = 0x02,
	SIEVE_OPCODE_JMPTRUE   = 0x03,
	SIEVE_OPCODE_JMPFALSE  = 0x04,
	SIEVE_OPCODE_STOP      = 0x05,
	SIEVE_OPCODE_KEEP      = 0x06,
	SIEVE_OPCODE_DISCARD   = 0x07,
	SIEVE_OPCODE_ADDRESS   = 0x08,
	SIEVE_OPCODE_HEADER    = 0x09, 
	SIEVE_OPCODE_EXISTS    = 0x10, 
	SIEVE_OPCODE_SIZEOVER  = 0x12,
	SIEVE_OPCODE_SIZEUNDER = 0x13
};

enum sieve_core_operand {
  SIEVE_OPERAND_NUMBER       = 0x01,
  SIEVE_OPERAND_STRING       = 0x02,
  SIEVE_OPERAND_STRING_LIST  = 0x03
};

#define SIEVE_OPCODE_CORE_MASK  0x1F
#define SIEVE_OPCODE_EXT_OFFSET (SIEVE_OPCODE_CORE_MASK + 1)

#define SIEVE_CORE_OPERAND_MASK 0x0F
#define SIEVE_CORE_OPERAND_BITS 4
 
void sieve_core_code_dump(struct sieve_interpreter *interpreter, sieve_size_t pc, int opcode);

#endif
