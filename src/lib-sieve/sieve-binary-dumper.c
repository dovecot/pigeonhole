#include "lib.h"

#include "sieve-common.h"
#include "sieve-binary.h"

#include "sieve-dump.h"

struct sieve_binary_dumper {
	pool_t pool;
	
	/* Dumptime environment */
	struct sieve_dumptime_env dumpenv; 
};

struct sieve_binary_dumper *sieve_binary_dumper_create
	(struct sieve_binary *sbin) 
{
	pool_t pool;
	struct sieve_binary_dumper *dumper;
	
	pool = pool_alloconly_create("sieve_binary_dumper", 4096);	
	dumper = p_new(pool, struct sieve_binary_dumper, 1);
	dumper->pool = pool;
	dumper->dumpenv.dumper = dumper;
	
	dumper->dumpenv.sbin = sbin;
	sieve_binary_ref(sbin);
	
	return dumper;
}

void sieve_binary_dumper_free(struct sieve_binary_dumper **dumper) 
{
	sieve_binary_unref(&(*dumper)->dumpenv.sbin);
	pool_unref(&((*dumper)->pool));
	
	*dumper = NULL;
}

pool_t sieve_binary_dumper_pool(struct sieve_binary_dumper *dumper)
{
	return dumper->pool;
}

/* */

void sieve_binary_dumper_run
	(struct sieve_binary_dumper *dumper, struct ostream *stream) 
{	
	dumper->dumpenv.stream = stream;
	
	dumper->dumpenv.cdumper = sieve_code_dumper_create(&(dumper->dumpenv));

	sieve_code_dumper_run(dumper->dumpenv.cdumper);
		
	sieve_code_dumper_free(&dumper->dumpenv.cdumper);	
}
