/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */
 
#ifndef __SIEVE_CODE_H
#define __SIEVE_CODE_H

#include "lib.h"
#include "buffer.h"
#include "mempool.h"
#include "array.h"

#include "sieve-common.h"

/* 
 * Coded string list 
 */

struct sieve_coded_stringlist;

bool sieve_coded_stringlist_next_item
	(struct sieve_coded_stringlist *strlist, string_t **str);
void sieve_coded_stringlist_reset
	(struct sieve_coded_stringlist *strlist);
bool sieve_coded_stringlist_read_all
	(struct sieve_coded_stringlist *strlist, pool_t pool,
		const char * const **list_r);

int sieve_coded_stringlist_get_length
	(struct sieve_coded_stringlist *strlist);
sieve_size_t sieve_coded_stringlist_get_end_address
	(struct sieve_coded_stringlist *strlist);
sieve_size_t sieve_coded_stringlist_get_current_offset
	(struct sieve_coded_stringlist *strlist);

/* 
 * Source line coding
 */

void sieve_code_source_line_emit
	(struct sieve_binary *sbin, unsigned int source_line);
bool sieve_code_source_line_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
bool sieve_code_source_line_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address, 
    	unsigned int *source_line);

/* 
 * Operand object
 */

struct sieve_operand_class {
	const char *name;
};

struct sieve_operand {
	const char *name;
	
	const struct sieve_extension *extension;
	unsigned int code;
	
	const struct sieve_operand_class *class;
	const void *interface;
};

sieve_size_t sieve_operand_emit_code
	(struct sieve_binary *sbin, const struct sieve_operand *opr);
const struct sieve_operand *sieve_operand_read
	(struct sieve_binary *sbin, sieve_size_t *address);

bool sieve_operand_optional_present
	(struct sieve_binary *sbin, sieve_size_t *address);
bool sieve_operand_optional_read	
	(struct sieve_binary *sbin, sieve_size_t *address, int *id_code);

/*
 * Core operands
 */
 
/* Operand codes */

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

/* Operand classes */

const struct sieve_operand_class number_class;
const struct sieve_operand_class string_class;
const struct sieve_operand_class stringlist_class;

/* Operand objects */

extern const struct sieve_operand number_operand;
extern const struct sieve_operand string_operand;
extern const struct sieve_operand stringlist_operand;

extern const struct sieve_operand *sieve_operands[];
extern const unsigned int sieve_operand_count;

/* Operand object interfaces */

struct sieve_opr_number_interface {
	bool (*dump)	
		(const struct sieve_dumptime_env *denv, sieve_size_t *address);
	bool (*read)
	  (const struct sieve_runtime_env *renv, sieve_size_t *address, 
	  	sieve_size_t *number);
};

struct sieve_opr_string_interface {
	bool (*dump)
		(const struct sieve_dumptime_env *denv, sieve_size_t *address);
	bool (*read)
		(const struct sieve_runtime_env *renv, sieve_size_t *address, 
			string_t **str);
};

struct sieve_opr_stringlist_interface {
	bool (*dump)
		(const struct sieve_dumptime_env *denv, sieve_size_t *address);
	struct sieve_coded_stringlist *(*read)
		(const struct sieve_runtime_env *renv, sieve_size_t *address);
};

/* 
 * Core operand functions 
 */

/* Number */

void sieve_opr_number_emit(struct sieve_binary *sbin, sieve_size_t number);
bool sieve_opr_number_dump_data	
	(const struct sieve_dumptime_env *denv, const struct sieve_operand *operand,
		sieve_size_t *address); 
bool sieve_opr_number_dump	
	(const struct sieve_dumptime_env *denv, sieve_size_t *address); 
bool sieve_opr_number_read_data
	(const struct sieve_runtime_env *renv, const struct sieve_operand *operand,
		sieve_size_t *address, sieve_size_t *number);
bool sieve_opr_number_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address, 
		sieve_size_t *number);

static inline bool sieve_operand_is_number
(const struct sieve_operand *operand)
{
	return ( operand != NULL && operand->class == &number_class );
}

/* String */

void sieve_opr_string_emit(struct sieve_binary *sbin, string_t *str);
bool sieve_opr_string_dump_data
	(const struct sieve_dumptime_env *denv, const struct sieve_operand *operand,
		sieve_size_t *address); 
bool sieve_opr_string_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address); 
bool sieve_opr_string_read_data
	(const struct sieve_runtime_env *renv, const struct sieve_operand *operand,
		sieve_size_t *address, string_t **str);
bool sieve_opr_string_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str);

static inline bool sieve_operand_is_string
(const struct sieve_operand *operand)
{
	return ( operand != NULL && operand->class == &string_class );
}

/* String list */

void sieve_opr_stringlist_emit_start
	(struct sieve_binary *sbin, unsigned int listlen, void **context);
void sieve_opr_stringlist_emit_item
	(struct sieve_binary *sbin, void *context ATTR_UNUSED, string_t *item);
void sieve_opr_stringlist_emit_end
	(struct sieve_binary *sbin, void *context);
bool sieve_opr_stringlist_dump_data
	(const struct sieve_dumptime_env *denv, const struct sieve_operand *operand, 
		sieve_size_t *address);
bool sieve_opr_stringlist_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
struct sieve_coded_stringlist *sieve_opr_stringlist_read_data
	(const struct sieve_runtime_env *renv, const struct sieve_operand *operand, 
		sieve_size_t op_address, sieve_size_t *address);
struct sieve_coded_stringlist *sieve_opr_stringlist_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

static inline bool sieve_operand_is_stringlist
(const struct sieve_operand *operand)
{
	return ( operand != NULL && 
		(operand->class == &stringlist_class || operand->class == &string_class) );
}

/*
 * Operation object
 */
 
struct sieve_operation {
	const char *mnemonic;
	
	const struct sieve_extension *extension;
	unsigned int code;
	
	bool (*dump)
		(const struct sieve_operation *op, 
			const struct sieve_dumptime_env *denv, sieve_size_t *address);
	int (*execute)
		(const struct sieve_operation *op, 
			const struct sieve_runtime_env *renv, sieve_size_t *address);
};

sieve_size_t sieve_operation_emit_code
	(struct sieve_binary *sbin, const struct sieve_operation *op);	
const struct sieve_operation *sieve_operation_read
	(struct sieve_binary *sbin, sieve_size_t *address);
const char *sieve_operation_read_string
    (struct sieve_binary *sbin, sieve_size_t *address);

/* 
 * Core operations 
 */

/* Opcodes */

enum sieve_operation_code {
	SIEVE_OPERATION_INVALID,
	SIEVE_OPERATION_JMP,
	SIEVE_OPERATION_JMPTRUE,
	SIEVE_OPERATION_JMPFALSE,
	
	SIEVE_OPERATION_STOP,
	SIEVE_OPERATION_KEEP,
	SIEVE_OPERATION_DISCARD,
	SIEVE_OPERATION_REDIRECT,
	
	SIEVE_OPERATION_ADDRESS,
	SIEVE_OPERATION_HEADER, 
	SIEVE_OPERATION_EXISTS, 
	SIEVE_OPERATION_SIZE_OVER,
	SIEVE_OPERATION_SIZE_UNDER,
	
	SIEVE_OPERATION_CUSTOM
};

/* Operation objects */

extern const struct sieve_operation sieve_jmp_operation;
extern const struct sieve_operation sieve_jmptrue_operation;
extern const struct sieve_operation sieve_jmpfalse_operation; 

extern const struct sieve_operation *sieve_operations[];
extern const unsigned int sieve_operations_count;

/*
 * Utilitity
 */

bool sieve_operation_string_dump
	(const struct sieve_operation *op,
		const struct sieve_dumptime_env *denv, sieve_size_t *address);

#endif
