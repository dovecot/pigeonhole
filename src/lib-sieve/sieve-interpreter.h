#ifndef __SIEVE_INTERPRETER_H__
#define __SIEVE_INTERPRETER_H__

#include "lib.h"
#include "array.h"
#include "buffer.h"
#include "mail-storage.h"

#include "sieve-common.h"

struct sieve_interpreter;

struct sieve_interpreter *sieve_interpreter_create(struct sieve_binary *binary);
void sieve_interpreter_free(struct sieve_interpreter *interpreter);

inline void sieve_interpreter_reset
	(struct sieve_interpreter *interpreter);
inline sieve_size_t sieve_interpreter_program_counter
	(struct sieve_interpreter *interpreter);
inline bool sieve_interpreter_program_jump
	(struct sieve_interpreter *interpreter, bool jump);
	
inline void sieve_interpreter_set_test_result
	(struct sieve_interpreter *interpreter, bool result);
inline bool sieve_interpreter_get_test_result
	(struct sieve_interpreter *interpreter);
	
inline struct sieve_binary *sieve_interpreter_get_binary
	(struct sieve_interpreter *interp);

/* Object registry */

struct sieve_interpreter_registry;

struct sieve_interpreter_registry *
	sieve_interpreter_registry_init(struct sieve_interpreter *interp, const char *name);
const void *sieve_interpreter_registry_get
	(struct sieve_interpreter_registry *reg, const struct sieve_extension *ext);
void  sieve_interpreter_registry_set
	(struct sieve_interpreter_registry *reg, const struct sieve_extension *ext, const void *obj);
	
/* Opcodes and operands */

bool sieve_interpreter_read_offset_operand
	(struct sieve_interpreter *interpreter, int *offset);

/* Stringlist Utility */

bool sieve_stringlist_match
	(struct sieve_coded_stringlist *key_list, const char *value, const struct sieve_comparator *cmp);

/* Accessing runtime information */

inline struct mail *sieve_interpreter_get_mail(struct sieve_interpreter *interpreter);

/* Code dump (debugging purposes) */

void sieve_interpreter_dump_code(struct sieve_interpreter *interp);

/* Code execute */

bool sieve_interpreter_execute_operation(struct sieve_interpreter *interp); 
struct sieve_result *sieve_interpreter_run
	(struct sieve_interpreter *interp, struct mail *mail);


#endif
