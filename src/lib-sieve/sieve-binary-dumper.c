#include "lib.h"
#include "str.h"
#include "ostream.h"

#include "sieve-common.h"
#include "sieve-extensions.h"
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

/* Formatted output */

void sieve_binary_dumpf
(const struct sieve_dumptime_env *denv, const char *fmt, ...)
{ 
	string_t *outbuf = t_str_new(128);
	va_list args;
	
	va_start(args, fmt);			
	str_vprintfa(outbuf, fmt, args);
	va_end(args);
	
	o_stream_send(denv->stream, str_data(outbuf), str_len(outbuf));
}

void sieve_binary_dump_sectionf
(const struct sieve_dumptime_env *denv, const char *fmt, ...)
{
	string_t *outbuf = t_str_new(128);
	va_list args;
	
	va_start(args, fmt);			
	str_printfa(outbuf, "\n* ");
	str_vprintfa(outbuf, fmt, args);
	str_printfa(outbuf, ":\n\n");
	va_end(args);
	
	o_stream_send(denv->stream, str_data(outbuf), str_len(outbuf));
}

/* Dumper execution */

void sieve_binary_dumper_run
	(struct sieve_binary_dumper *dumper, struct ostream *stream) 
{	
	struct sieve_binary *sbin = dumper->dumpenv.sbin;
	struct sieve_dumptime_env *denv = &(dumper->dumpenv);
	int count, i;
	
	dumper->dumpenv.stream = stream;
	
	/* Dump list of used extensions */
	
	count = sieve_binary_extensions_count(sbin);
	if ( count > 0 ) {
		sieve_binary_dump_sectionf(denv, "Required extensions");
	
		for ( i = 0; i < count; i++ ) {
			int ext_id;
			const struct sieve_extension *ext = sieve_binary_extension_get_by_index
				(sbin, i, &ext_id);
			sieve_binary_dumpf(denv, "%d: %s (%d)\n", i, ext->name, ext_id);
		}
	}
	
	/* Dump main program */
	
	sieve_binary_dump_sectionf(denv, "Main program");
	
	dumper->dumpenv.cdumper = sieve_code_dumper_create(&(dumper->dumpenv));

	sieve_code_dumper_run(dumper->dumpenv.cdumper);
		
	sieve_code_dumper_free(&dumper->dumpenv.cdumper);	
	
	/* Dump extension-specific elements of the binary */
	
	count = sieve_binary_extensions_count(sbin);
	if ( count > 0 ) {	
		for ( i = 0; i < count; i++ ) {
			int ext_id;
			const struct sieve_extension *ext = sieve_binary_extension_get_by_index
				(sbin, i, &ext_id);
	
			if ( ext->binary_dump != NULL ) {	
				if ( !ext->binary_dump(denv) ) break;
			}
		}
	}
}
