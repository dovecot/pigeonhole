#ifndef __SIEVE_CODE_H
#define __SIEVE_CODE_H

#include <lib.h>
#include <buffer.h>
#include <array.h>

#include "sieve-common.h"

/* String list */

struct sieve_coded_stringlist;

bool sieve_coded_stringlist_next_item(struct sieve_coded_stringlist *strlist, string_t **str);
void sieve_coded_stringlist_reset(struct sieve_coded_stringlist *strlist);

inline int sieve_coded_stringlist_get_length(struct sieve_coded_stringlist *strlist);
inline sieve_size_t sieve_coded_stringlist_get_end_address(struct sieve_coded_stringlist *strlist);
inline sieve_size_t sieve_coded_stringlist_get_current_offset(struct sieve_coded_stringlist *strlist);

/* Operand: argument to an opcode */

struct sieve_operand_class {
	const char *name;
	
	const void *interface;
};

struct sieve_operand {
	const char *name;
	
	const struct sieve_operand_class *class;

	unsigned int positional:1;
};

struct sieve_opr_number_interface {
	bool (*dump)	
		(struct sieve_binary *sbin, sieve_size_t *address);
	bool (*read)
	  (struct sieve_binary *sbin, sieve_size_t *address, sieve_size_t *number);
};

struct sieve_opr_string_interface {
	bool (*dump)
		(struct sieve_binary *sbin, sieve_size_t *address);
	bool (*read)
		(struct sieve_binary *sbin, sieve_size_t *address, string_t **str);
};

struct sieve_opr_stringlist_interface {
	bool (*dump)
		(struct sieve_binary *sbin, sieve_size_t *address);
	struct sieve_coded_stringlist *(*read)
		(struct sieve_binary *sbin, sieve_size_t *address);
};

enum sieve_core_operand {
	SIEVE_OPERAND_OPTIONAL,
	SIEVE_OPERAND_NUMBER,
	SIEVE_OPERAND_STRING,
	SIEVE_OPERAND_STRING_LIST,
	SIEVE_OPERAND_COMPARATOR,
	SIEVE_OPERAND_MATCH_TYPE,
	SIEVE_OPERAND_ADDRESS_PART,

	SIEVE_OPERAND_CUSTOM
};

extern const struct sieve_operand *sieve_operands[];
extern const unsigned int sieve_operand_count;

inline sieve_size_t sieve_operand_emit_code
	(struct sieve_binary *sbin, int operand);
const struct sieve_operand *sieve_operand_read
	(struct sieve_binary *sbin, sieve_size_t *address);

bool sieve_operand_optional_present(struct sieve_binary *sbin, sieve_size_t *address);
unsigned int sieve_operand_optional_read
	(struct sieve_binary *sbin, sieve_size_t *address);

void sieve_opr_number_emit(struct sieve_binary *sbin, sieve_size_t number);
bool sieve_opr_number_dump(struct sieve_binary *sbin, sieve_size_t *address); 
bool sieve_opr_number_read
	(struct sieve_binary *sbin, sieve_size_t *address, sieve_size_t *number);

void sieve_opr_string_emit(struct sieve_binary *sbin, string_t *str);
bool sieve_opr_string_dump(struct sieve_binary *sbin, sieve_size_t *address); 
bool sieve_opr_string_read
	(struct sieve_binary *sbin, sieve_size_t *address, string_t **str);

void sieve_opr_stringlist_emit_start
	(struct sieve_binary *sbin, unsigned int listlen, void **context);
void sieve_opr_stringlist_emit_item
	(struct sieve_binary *sbin, void *context ATTR_UNUSED, string_t *item);
void sieve_opr_stringlist_emit_end
	(struct sieve_binary *sbin, void *context);
bool sieve_opr_stringlist_dump(struct sieve_binary *sbin, sieve_size_t *address);
struct sieve_coded_stringlist *sieve_opr_stringlist_read
	(struct sieve_binary *sbin, sieve_size_t *address);


/* Opcode: identifies what's to be done */

enum sieve_operation_code {
	SIEVE_OPCODE_JMP,
	SIEVE_OPCODE_JMPTRUE,
	SIEVE_OPCODE_JMPFALSE,
	
	SIEVE_OPCODE_STOP,
	SIEVE_OPCODE_KEEP,
	SIEVE_OPCODE_DISCARD,
	SIEVE_OPCODE_REDIRECT,
	
	SIEVE_OPCODE_ADDRESS,
	SIEVE_OPCODE_HEADER, 
	SIEVE_OPCODE_EXISTS, 
	SIEVE_OPCODE_SIZE_OVER,
	SIEVE_OPCODE_SIZE_UNDER,
	
	SIEVE_OPCODE_CUSTOM
};

struct sieve_opcode {
	const char *mnemonic;
	
	enum sieve_operation_code code;
	
	const struct sieve_extension *extension;
	unsigned int ext_code;
	
	bool (*dump)
		(const struct sieve_opcode *opcode, 
			const struct sieve_runtime_env *renv, sieve_size_t *address);
	bool (*execute)
		(const struct sieve_opcode *opcode, 
			const struct sieve_runtime_env *renv, sieve_size_t *address);
};

extern const struct sieve_opcode *sieve_opcodes[];
extern const unsigned int sieve_opcode_count;

extern const struct sieve_opcode sieve_jmp_opcode;
extern const struct sieve_opcode sieve_jmptrue_opcode;
extern const struct sieve_opcode sieve_jmpfalse_opcode; 

inline sieve_size_t sieve_operation_emit_code
	(struct sieve_binary *sbin, const struct sieve_opcode *op);
inline sieve_size_t sieve_operation_emit_code_ext
	(struct sieve_binary *sbin, const struct sieve_opcode *op, int ext_id);	
const struct sieve_opcode *sieve_operation_read
	(struct sieve_binary *sbin, sieve_size_t *address);

bool sieve_opcode_string_dump
	(const struct sieve_opcode *opcode,
		const struct sieve_runtime_env *renv, sieve_size_t *address);

/* Core operands */

#endif
