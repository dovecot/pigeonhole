#ifndef __SIEVE_DUMP_H
#define __SIEVE_DUMP_H

#include "sieve-common.h"

#include "sieve-binary-dumper.h"
#include "sieve-code-dumper.h"

struct sieve_dumptime_env {
	struct sieve_binary_dumper *dumper;
	struct sieve_code_dumper *cdumper;
	struct sieve_binary *sbin;
	
	struct ostream *stream;
};

#endif /* __SIEVE_DUMP_H */
