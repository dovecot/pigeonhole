/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __SIEVE_DUMP_H
#define __SIEVE_DUMP_H

#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-binary-dumper.h"
#include "sieve-code-dumper.h"

/*
 * Dumptime environment
 */

struct sieve_dumptime_env {
	struct sieve_binary_dumper *dumper;
	struct sieve_code_dumper *cdumper;

	struct sieve_instance *svinst;

	struct sieve_binary *sbin;
	struct sieve_binary_block *sblock;

	struct sieve_operation oprtn;
	
	struct ostream *stream;
};

#endif /* __SIEVE_DUMP_H */
