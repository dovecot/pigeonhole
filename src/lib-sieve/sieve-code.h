/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
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
	(struct sieve_coded_stringlist *strlist, string_t **str_r);
void sieve_coded_stringlist_reset
	(struct sieve_coded_stringlist *strlist);
bool sieve_coded_stringlist_read_all
	(struct sieve_coded_stringlist *strlist, pool_t pool,
		const char * const **list_r);

unsigned int sieve_coded_stringlist_get_length
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
    	unsigned int *source_line_r);

/* 
 * Operand object
 */

struct sieve_operand_class {
	const char *name;
};

struct sieve_operand_def {
	const char *name;
	
	const struct sieve_extension_def *ext_def;
	unsigned int code;
	
	const struct sieve_operand_class *class;
	const void *interface;
};

struct sieve_operand {
	const struct sieve_operand_def *def;
	const struct sieve_extension *ext;
	sieve_size_t address;
};

#define sieve_operand_name(opr) \
	( (opr)->def == NULL ? "(NULL)" : (opr)->def->name )
#define sieve_operand_is(opr, definition) \
	( (opr)->def == &(definition) )

sieve_size_t sieve_operand_emit
	(struct sieve_binary *sbin, const struct sieve_extension *ext,
		const struct sieve_operand_def *oprnd);
bool sieve_operand_read
	(struct sieve_binary *sbin, sieve_size_t *address, 
		struct sieve_operand *oprnd);

bool sieve_operand_optional_present
	(struct sieve_binary *sbin, sieve_size_t *address);
bool sieve_operand_optional_read	
	(struct sieve_binary *sbin, sieve_size_t *address, 
		signed int *id_code);

/*
 * Core operands
 */
 
/* Operand codes */

enum sieve_core_operand {
	SIEVE_OPERAND_OPTIONAL = 0x00,
	SIEVE_OPERAND_NUMBER,
	SIEVE_OPERAND_STRING,
	SIEVE_OPERAND_STRING_LIST,
	SIEVE_OPERAND_COMPARATOR,
	SIEVE_OPERAND_MATCH_TYPE,
	SIEVE_OPERAND_ADDRESS_PART,
	SIEVE_OPERAND_CATENATED_STRING,

	SIEVE_OPERAND_CUSTOM
};

/* Operand classes */

extern const struct sieve_operand_class number_class;
extern const struct sieve_operand_class string_class;
extern const struct sieve_operand_class stringlist_class;

/* Operand objects */

extern const struct sieve_operand_def omitted_operand;
extern const struct sieve_operand_def number_operand;
extern const struct sieve_operand_def string_operand;
extern const struct sieve_operand_def stringlist_operand;
extern const struct sieve_operand_def catenated_string_operand;

extern const struct sieve_operand_def *sieve_operands[];
extern const unsigned int sieve_operand_count;

/* Operand object interfaces */

struct sieve_opr_number_interface {
	bool (*dump)	
		(const struct sieve_dumptime_env *denv, sieve_size_t *address,
			const char *field_name);
	bool (*read)
	  (const struct sieve_runtime_env *renv, sieve_size_t *address, 
	  	sieve_number_t *number_r);
};

struct sieve_opr_string_interface {
	bool (*dump)
		(const struct sieve_dumptime_env *denv, const struct sieve_operand *operand, 
			sieve_size_t *address, const char *field_name);
	bool (*read)
		(const struct sieve_runtime_env *renv, const struct sieve_operand *operand, 
		 	sieve_size_t *address, string_t **str_r);
};

struct sieve_opr_stringlist_interface {
	bool (*dump)
		(const struct sieve_dumptime_env *denv, sieve_size_t *address,
			const char *field_name);
	struct sieve_coded_stringlist *(*read)
		(const struct sieve_runtime_env *renv, sieve_size_t *address);
};

/* 
 * Core operand functions 
 */

/* Omitted */

void sieve_opr_omitted_emit(struct sieve_binary *sbin);

static inline bool sieve_operand_is_omitted
(const struct sieve_operand *operand)
{
	return ( operand != NULL && operand->def != NULL &&
		operand->def == &omitted_operand );
}

/* Number */

void sieve_opr_number_emit(struct sieve_binary *sbin, sieve_number_t number);
bool sieve_opr_number_dump_data	
	(const struct sieve_dumptime_env *denv, const struct sieve_operand *operand,
		sieve_size_t *address, const char *field_name); 
bool sieve_opr_number_dump	
	(const struct sieve_dumptime_env *denv, sieve_size_t *address,
		const char *field_name); 
bool sieve_opr_number_read_data
	(const struct sieve_runtime_env *renv, const struct sieve_operand *operand,
		sieve_size_t *address, sieve_number_t *number_r);
bool sieve_opr_number_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address, 
		sieve_number_t *number_r);

static inline bool sieve_operand_is_number
(const struct sieve_operand *operand)
{
	return ( operand != NULL && operand->def != NULL && 
		operand->def->class == &number_class );
}

/* String */

void sieve_opr_string_emit(struct sieve_binary *sbin, string_t *str);
bool sieve_opr_string_dump_data
	(const struct sieve_dumptime_env *denv, const struct sieve_operand *operand,
		sieve_size_t *address, const char *field_name); 
bool sieve_opr_string_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address,
		const char *field_name); 
bool sieve_opr_string_dump_ex
	(const struct sieve_dumptime_env *denv, sieve_size_t *address, 
		const char *field_name, bool *literal_r); 
bool sieve_opr_string_read_data
	(const struct sieve_runtime_env *renv, const struct sieve_operand *operand,
		sieve_size_t *address, string_t **str_r);
bool sieve_opr_string_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str_r);
bool sieve_opr_string_read_ex
	(const struct sieve_runtime_env *renv, sieve_size_t *address, string_t **str_r,
		bool *literal_r);

static inline bool sieve_operand_is_string
(const struct sieve_operand *operand)
{
	return ( operand != NULL && operand->def != NULL &&
		operand->def->class == &string_class );
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
		sieve_size_t *address, const char *field_name);
bool sieve_opr_stringlist_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address,
		const char *field_name);
struct sieve_coded_stringlist *sieve_opr_stringlist_read_data
	(const struct sieve_runtime_env *renv, const struct sieve_operand *operand, 
		sieve_size_t *address);
struct sieve_coded_stringlist *sieve_opr_stringlist_read
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

static inline bool sieve_operand_is_stringlist
(const struct sieve_operand *operand)
{
	return ( operand != NULL && operand->def != NULL &&
		(operand->def->class == &stringlist_class || 
			operand->def->class == &string_class) );
}

/* Catenated string */

void sieve_opr_catenated_string_emit
	(struct sieve_binary *sbin, unsigned int elements);
	
/*
 * Operation object
 */
 
struct sieve_operation_def {
	const char *mnemonic;
	
	const struct sieve_extension_def *ext_def;
	unsigned int code;
	
	bool (*dump)
		(const struct sieve_dumptime_env *denv, sieve_size_t *address);
	int (*execute)
		(const struct sieve_runtime_env *renv, sieve_size_t *address);
};

struct sieve_operation {
	const struct sieve_operation_def *def;
	const struct sieve_extension *ext;

	sieve_size_t address;
};

#define sieve_operation_is(oprtn, definition) \
	( (oprtn)->def == &(definition) )
#define sieve_operation_mnemonic(oprtn) \
	( (oprtn)->def == NULL ? "(NULL)" : (oprtn)->def->mnemonic )

sieve_size_t sieve_operation_emit
	(struct sieve_binary *sbin, const struct sieve_extension *ext,
		const struct sieve_operation_def *op_def);	
bool sieve_operation_read
	(struct sieve_binary *sbin, sieve_size_t *address,
		struct sieve_operation *oprtn);
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

extern const struct sieve_operation_def sieve_jmp_operation;
extern const struct sieve_operation_def sieve_jmptrue_operation;
extern const struct sieve_operation_def sieve_jmpfalse_operation; 

extern const struct sieve_operation_def *sieve_operations[];
extern const unsigned int sieve_operations_count;

#endif
