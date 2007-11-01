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

enum sieve_core_operand {
  SIEVE_OPERAND_NUMBER,
  SIEVE_OPERAND_STRING,
  SIEVE_OPERAND_STRING_LIST,
  SIEVE_OPERAND_COMPARATOR,
  SIEVE_OPERAND_MATCH_TYPE,
  SIEVE_OPERAND_ADDR_PART  
};

/* Operand: argument to and opcode */
struct sieve_operand {
	/* Interpreter */
	bool (*dump)(struct sieve_interpreter *interpreter);
};

struct sieve_operand_string {
	struct sieve_operand operand;
	
	void (*emit)(struct sieve_generator *generator, string_t *str);
	bool (*read)(struct sieve_interpreter *interpreter, string_t **str)
};

void sieve_operand_number_emit(struct sieve_generator *generator, sieve_size_t number);
void sieve_operand_string_emit(struct sieve_generator *generator, string_t *str);
void sieve_operand_stringlist_emit_start
	(struct sieve_generator *generator, unsigned int listlen, void **context);
void sieve_operand_stringlist_emit_item
	(struct sieve_generator *generator, void *context ATTR_UNUSED, string_t *item);
void sieve_operand_stringlist_emit_end
	(struct sieve_generator *generator, void *context);

#define SIEVE_OPCODE_CORE_MASK  0x1F
#define SIEVE_OPCODE_EXT_OFFSET (SIEVE_OPCODE_CORE_MASK + 1)

#define SIEVE_OPERAND_CORE_MASK 0x1F

#endif
