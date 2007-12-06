#ifndef __SIEVE_CODE_DUMPER_H
#define __SIEVE_CODE_DUMPER_H

#include "sieve-common.h"

struct sieve_code_dumper;

struct sieve_dumptime_env {
	struct sieve_code_dumper *dumper;
	struct sieve_binary *sbin;
	
	struct ostream *stream;
};

struct sieve_code_dumper *sieve_code_dumper_create(struct sieve_binary *sbin);
void sieve_code_dumper_free(struct sieve_code_dumper *dumper);
inline pool_t sieve_code_dumper_pool(struct sieve_code_dumper *dumper);
	
/*  */	
	
void sieve_code_dumpf
	(const struct sieve_dumptime_env *denv, const char *fmt, ...)
		ATTR_FORMAT(2, 3);

inline void sieve_code_mark(const struct sieve_dumptime_env *denv);
inline void sieve_code_mark_specific
	(const struct sieve_dumptime_env *denv, sieve_size_t location);
inline void sieve_code_descend(const struct sieve_dumptime_env *denv);
inline void sieve_code_ascend(const struct sieve_dumptime_env *denv);

/* Opcodes and operands */
	
bool sieve_code_dumper_print_optional_operands
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);

/* Code dump (debugging purposes) */

void sieve_code_dumper_run
	(struct sieve_code_dumper *dumper, struct ostream *stream);

#endif /* __SIEVE_CODE_DUMPER_H */
