#include <stdio.h>
#include <string.h>

#include "lib.h"
#include "mempool.h"

#include "sieve-extensions.h"
#include "sieve-commands-private.h"
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
	
	/* Runtime environment environment */
	struct sieve_dumptime_env dumpenv; 
};

struct sieve_code_dumper *sieve_code_dumper_create(struct sieve_binary *sbin) 
{
	pool_t pool;
	struct sieve_code_dumper *dumpr;
	
	pool = pool_alloconly_create("sieve_code_dumper", 4096);	
	dumpr = p_new(pool, struct sieve_code_dumper, 1);
	dumpr->pool = pool;
	dumpr->dumpenv.dumpr = dumpr;
	
	dumpr->dumpenv.sbin = sbin;
	sieve_binary_ref(sbin);
	
	dumpr->pc = 0;
	
	return dumpr;
}

void sieve_code_dumper_free(struct sieve_code_dumper *dumpr) 
{
	sieve_binary_unref(&dumpr->dumpenv.sbin);
	pool_unref(&(dumpr->pool));
}

inline pool_t sieve_code_dumper_pool(struct sieve_code_dumper *dumpr)
{
	return dumpr->pool;
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
				const struct sieve_side_effect *seffect = 
					sieve_opr_side_effect_read(denv->sbin, address);

				if ( seffect == NULL ) return FALSE;
			
				if ( seffect->read != NULL && !seffect->dump(seffect, denv, address) )
					return FALSE;
			}
		}
	}
	return TRUE;
}
 
/* Code Dump */

static bool sieve_code_dumper_print_operation
	(struct sieve_code_dumper *dumpr) 
{
	const struct sieve_opcode *opcode = 
		sieve_operation_read(dumpr->dumpenv.sbin, &(dumpr->pc));

	if ( opcode != NULL ) {
		printf("%08x: ", dumpr->pc-1);
	
		if ( opcode->dump != NULL )
			return opcode->dump(opcode, &(dumpr->dumpenv), &(dumpr->pc));
		else if ( opcode->mnemonic != NULL )
			printf("%s\n", opcode->mnemonic);
		else
			return FALSE;
			
		return TRUE;
	}		
	
	return FALSE;
}

void sieve_code_dumper_run(struct sieve_code_dumper *dumpr) 
{
	dumpr->pc = 0;
	
	while ( dumpr->pc < 
		sieve_binary_get_code_size(dumpr->dumpenv.sbin) ) {
		if ( !sieve_code_dumper_print_operation(dumpr) ) {
			printf("Binary is corrupt.\n");
			return;
		}
	}
	
	printf("%08x: [End of code]\n", 
		sieve_binary_get_code_size(dumpr->dumpenv.sbin));	
}
