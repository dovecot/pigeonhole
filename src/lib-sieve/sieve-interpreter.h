#ifndef __SIEVE_INTERPRETER_H__
#define __SIEVE_INTERPRETER_H__

#include "lib.h"
#include "buffer.h"
#include "mail-storage.h"

#include "sieve-binary.h"

struct sieve_coded_stringlist;

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

/* Opcodes and operands */

bool sieve_interpreter_read_offset_operand
	(struct sieve_interpreter *interpreter, int *offset);
bool sieve_interpreter_read_number_operand
  (struct sieve_interpreter *interpreter, sieve_size_t *number);
bool sieve_interpreter_read_string_operand
  (struct sieve_interpreter *interpreter, string_t **str);
struct sieve_coded_stringlist *sieve_interpreter_read_stringlist_operand
	(struct sieve_interpreter *interpreter);
	
/* Stringlist Utility */

bool sieve_stringlist_match
	(struct sieve_coded_stringlist *key_list, const char *value);

/* Accessing runtime information */

inline struct mail *sieve_interpreter_get_mail(struct sieve_interpreter *interpreter);

/* Code dump (debugging purposes) */

bool sieve_interpreter_dump_number(struct sieve_interpreter *interpreter);
bool sieve_interpreter_dump_string(struct sieve_interpreter *interpreter);
bool sieve_interpreter_dump_string_list(struct sieve_interpreter *interpreter);
bool sieve_interpreter_dump_operand(struct sieve_interpreter *interpreter);

void sieve_interpreter_dump_code(struct sieve_interpreter *interpreter);

/* Code execute */

bool sieve_interpreter_execute_opcode(struct sieve_interpreter *interpreter); 
struct sieve_result *sieve_interpreter_run
	(struct sieve_interpreter *interpreter, struct mail *mail);


#endif
