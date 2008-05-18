#ifndef __SIEVE_BINARY_DUMPER_H
#define __SIEVE_BINARY_DUMPER_H

#include "sieve-common.h"

struct sieve_binary_dumper;

struct sieve_binary_dumper *sieve_binary_dumper_create
	(struct sieve_binary *sbin);
void sieve_binary_dumper_free
	(struct sieve_binary_dumper **dumper);

pool_t sieve_binary_dumper_pool
	(struct sieve_binary_dumper *dumper);

/* */

void sieve_binary_dumper_run
	(struct sieve_binary_dumper *dumper, struct ostream *stream);


#endif /* __SIEVE_BINARY_DUMPER_H */
