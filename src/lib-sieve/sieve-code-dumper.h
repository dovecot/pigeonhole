#ifndef __SIEVE_CODE_DUMPER_H
#define __SIEVE_CODE_DUMPER_H

#include "sieve-common.h"

struct sieve_code_dumper;

struct sieve_dumptime_env {
	struct sieve_code_dumper *dumpr;
	struct sieve_binary *sbin;
	
	const struct sieve_opcode *opcode;
	sieve_size_t *opcode_addr;
};

struct sieve_code_dumper *sieve_code_dumper_create(struct sieve_binary *sbin);
void sieve_code_dumper_free(struct sieve_code_dumper *dumpr);
inline pool_t sieve_code_dumper_pool(struct sieve_code_dumper *dumpr);
	
/* Opcodes and operands */
	
bool sieve_code_dumper_print_optional_operands
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);

/* Code dump (debugging purposes) */

void sieve_code_dumper_run(struct sieve_code_dumper *dumpr);

#endif /* __SIEVE_CODE_DUMPER_H */
