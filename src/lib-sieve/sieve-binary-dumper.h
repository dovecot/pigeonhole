/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __SIEVE_BINARY_DUMPER_H
#define __SIEVE_BINARY_DUMPER_H

#include "sieve-common.h"

/*
 * Binary dumper object
 */

struct sieve_binary_dumper;

struct sieve_binary_dumper *sieve_binary_dumper_create
	(struct sieve_binary *sbin);
void sieve_binary_dumper_free
	(struct sieve_binary_dumper **dumper);

pool_t sieve_binary_dumper_pool
	(struct sieve_binary_dumper *dumper);

/* 
 * Formatted output 
 */

void sieve_binary_dumpf
	(const struct sieve_dumptime_env *denv, const char *fmt, ...);
void sieve_binary_dump_sectionf
	(const struct sieve_dumptime_env *denv, const char *fmt, ...);

/*
 * Dumping the binary
 */

bool sieve_binary_dumper_run
	(struct sieve_binary_dumper *dumper, struct ostream *stream);


#endif /* __SIEVE_BINARY_DUMPER_H */
