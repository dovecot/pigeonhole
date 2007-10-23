#ifndef __SIEVE_INTERPRETER_H__
#define __SIEVE_INTERPRETER_H__

#include "lib.h"
#include "buffer.h"
#include "mail-storage.h"

#include "sieve-binary.h"

struct sieve_coded_stringlist;

struct sieve_interpreter;

struct sieve_mail_context {
	struct mail_namespace *namespaces;
	struct mail_storage **storage_r;
	struct mail *mail;
	const char *destaddr;
	const char *mailbox;
};

struct sieve_interpreter *sieve_interpreter_create(struct sieve_binary *binary);
void sieve_interpreter_free(struct sieve_interpreter *interpreter);

int sieve_interpreter_read_offset(struct sieve_interpreter *interpreter);
bool sieve_interpreter_read_integer
  (struct sieve_interpreter *interpreter, sieve_size_t *integer); 
bool sieve_interpreter_read_string
  (struct sieve_interpreter *interpreter, string_t **str);
  
bool sieve_interpreter_read_stringlist
  (struct sieve_interpreter *interpreter, struct sieve_coded_stringlist **strlist);
bool sieve_coded_stringlist_read_item
	(struct sieve_coded_stringlist *strlist, string_t **str);  

/* Code dump (debugging purposes) */

void sieve_interpreter_dump_number(struct sieve_interpreter *interpreter);
void sieve_interpreter_dump_string(struct sieve_interpreter *interpreter);
void sieve_interpreter_dump_string_list(struct sieve_interpreter *interpreter);
void sieve_interpreter_dump_operand(struct sieve_interpreter *interpreter);

void sieve_interpreter_dump_code(struct sieve_interpreter *interpreter);

void sieve_interpreter_reset(struct sieve_interpreter *interpreter);

#endif
