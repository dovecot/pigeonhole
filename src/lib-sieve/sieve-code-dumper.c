#include <stdio.h>
#include <string.h>

#include "lib.h"
#include "str.h"
#include "mempool.h"
#include "ostream.h"

#include "sieve-extensions.h"
#include "sieve-commands-private.h"
#include "sieve-code.h"
#include "sieve-actions.h"
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-result.h"
#include "sieve-comparators.h"

#include "sieve-code-dumper.h"

struct sieve_code_dumper {
	pool_t pool;
					
	/* Dump status */
	sieve_size_t pc;          /* Program counter */
	
	const struct sieve_opcode *opcode;
	sieve_size_t mark_address;
	unsigned int indent;
	
	/* Runtime environment environment */
	struct sieve_dumptime_env dumpenv; 
};

struct sieve_code_dumper *sieve_code_dumper_create(struct sieve_binary *sbin) 
{
	pool_t pool;
	struct sieve_code_dumper *dumper;
	
	pool = pool_alloconly_create("sieve_code_dumper", 4096);	
	dumper = p_new(pool, struct sieve_code_dumper, 1);
	dumper->pool = pool;
	dumper->dumpenv.dumper = dumper;
	
	dumper->dumpenv.sbin = sbin;
	sieve_binary_ref(sbin);
	
	dumper->pc = 0;
	
	return dumper;
}

void sieve_code_dumper_free(struct sieve_code_dumper *dumper) 
{
	sieve_binary_unref(&dumper->dumpenv.sbin);
	pool_unref(&(dumper->pool));
}

inline pool_t sieve_code_dumper_pool(struct sieve_code_dumper *dumper)
{
	return dumper->pool;
}

/* Dump functions */

void sieve_code_dumpf
(const struct sieve_dumptime_env *denv, const char *fmt, ...)
{
	unsigned tab = denv->dumper->indent;
	 
	string_t *outbuf = t_str_new(128);
	va_list args;
	
	va_start(args, fmt);	
	str_printfa(outbuf, "%08x: ", denv->dumper->mark_address);
	
	while ( tab > 0 )	{
		str_append(outbuf, "  ");
		tab--;
	}
	
	str_vprintfa(outbuf, fmt, args);
	str_append_c(outbuf, '\n');
	va_end(args);
	
	o_stream_send(denv->stream, str_data(outbuf), str_len(outbuf));
}

inline void sieve_code_mark(const struct sieve_dumptime_env *denv)
{
	denv->dumper->mark_address = denv->dumper->pc;
}

inline void sieve_code_mark_specific
(const struct sieve_dumptime_env *denv, sieve_size_t location)
{
	denv->dumper->mark_address = location;
}

inline void sieve_code_descend(const struct sieve_dumptime_env *denv)
{
	denv->dumper->indent++;
}

inline void sieve_code_ascend(const struct sieve_dumptime_env *denv)
{
	if ( denv->dumper->indent > 0 )
		denv->dumper->indent--;
}

/* Opcodes and operands */

bool sieve_code_dumper_print_optional_operands
	(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	int opt_code;
	
	if ( sieve_operand_optional_present(denv->sbin, address) ) {
		while ( opt_code != 0 ) {
			if ( !sieve_operand_optional_read(denv->sbin, address, &opt_code) )
				return FALSE;

			if ( opt_code == SIEVE_OPT_SIDE_EFFECT ) {
				if ( !sieve_opr_side_effect_dump(denv, address) )
					return FALSE;
			}
		}
	}
	return TRUE;
}
 
/* Code Dump */

static bool sieve_code_dumper_print_operation
	(struct sieve_code_dumper *dumper) 
{	
	const struct sieve_opcode *opcode;
	
	/* Mark start address of opcode */
	dumper->indent = 0;
	dumper->mark_address = dumper->pc;

	/* Read opcode */
	dumper->opcode = opcode = 
		sieve_operation_read(dumper->dumpenv.sbin, &(dumper->pc));

	/* Try to dump it */
	if ( opcode != NULL ) {
		if ( opcode->dump != NULL )
			return opcode->dump(opcode, &(dumper->dumpenv), &(dumper->pc));
		else if ( opcode->mnemonic != NULL )
			sieve_code_dumpf(&(dumper->dumpenv), "%s", opcode->mnemonic);
		else
			return FALSE;
			
		return TRUE;
	}		
	
	return FALSE;
}

void sieve_code_dumper_run
	(struct sieve_code_dumper *dumper, struct ostream *stream) 
{
	dumper->pc = 0;
	dumper->dumpenv.stream = stream;
	
	while ( dumper->pc < 
		sieve_binary_get_code_size(dumper->dumpenv.sbin) ) {
		if ( !sieve_code_dumper_print_operation(dumper) ) {
			sieve_code_dumpf(&(dumper->dumpenv), "Binary is corrupt.");
			return;
		}
	}
	
	/* Mark end of the binary */
	dumper->indent = 0;
	dumper->mark_address = sieve_binary_get_code_size(dumper->dumpenv.sbin);
	sieve_code_dumpf(&(dumper->dumpenv), "[End of code]");	

	/* Add empty line to the file */
	o_stream_send_str(dumper->dumpenv.stream, "\n");
}
