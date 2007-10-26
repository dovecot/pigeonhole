#ifndef __SIEVE_CODE_H
#define __SIEVE_CODE_H

#include <lib.h>
#include <buffer.h>
#include <array.h>

#include "sieve-common.h"

typedef size_t sieve_size_t;

/* Opcode: identifies what's to be done */
struct sieve_opcode {
	bool (*dump)(struct sieve_interpreter *interpreter);
	bool (*execute)(struct sieve_interpreter *interpreter);
};

enum sieve_core_operation {
	SIEVE_OPCODE_JMP,
	SIEVE_OPCODE_JMPTRUE,
	SIEVE_OPCODE_JMPFALSE,
	SIEVE_OPCODE_STOP,
	SIEVE_OPCODE_KEEP,
	SIEVE_OPCODE_DISCARD,
	SIEVE_OPCODE_ADDRESS,
	SIEVE_OPCODE_HEADER, 
	SIEVE_OPCODE_EXISTS, 
	SIEVE_OPCODE_SIZEOVER,
	SIEVE_OPCODE_SIZEUNDER
};

extern const struct sieve_opcode *sieve_opcodes[];
extern const unsigned int sieve_opcode_count;

/* Operand: argument to and opcode */
struct sieve_operand {
	bool (*dump)(struct sieve_interpreter *interpreter);
	bool (*execute)(struct sieve_interpreter *interpreter);
};

enum sieve_core_operand {
  SIEVE_OPERAND_NUMBER,
  SIEVE_OPERAND_STRING,
  SIEVE_OPERAND_STRING_LIST,
  SIEVE_OPERAND_COMPARATOR,
  SIEVE_OPERAND_MATCH_TYPE,
  SIEVE_OPERAND_ADDR_PART  
};

#define SIEVE_OPCODE_CORE_MASK  0x1F
#define SIEVE_OPCODE_EXT_OFFSET (SIEVE_OPCODE_CORE_MASK + 1)

#define SIEVE_CORE_OPERAND_MASK 0x0F
#define SIEVE_CORE_OPERAND_BITS 4

#endif
