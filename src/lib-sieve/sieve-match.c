/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "mempool.h"
#include "hash.h"
#include "array.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-binary.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"

#include "sieve-match.h"

/*
 * Matching implementation
 */

struct sieve_match_context *sieve_match_begin
(struct sieve_interpreter *interp, const struct sieve_match_type *mtch, 
	const struct sieve_comparator *cmp, 
	const struct sieve_match_key_extractor *kextract,
	struct sieve_coded_stringlist *key_list)
{
	struct sieve_match_context *mctx = t_new(struct sieve_match_context, 1);  

	mctx->interp = interp;
	mctx->match_type = mtch;
	mctx->comparator = cmp;
	mctx->kextract = kextract;
	mctx->key_list = key_list;

	if ( mtch->match_init != NULL ) {
		mtch->match_init(mctx);
	}

	return mctx;
}

int sieve_match_value
	(struct sieve_match_context *mctx, const char *value, size_t val_size)
{
	const struct sieve_match_type *mtch = mctx->match_type;
	sieve_coded_stringlist_reset(mctx->key_list);
	bool ok = TRUE;

	/* Reject unimplemented match-type */
	if ( mtch->match == NULL )
		return FALSE;
				
	/* Match to all key values */
	if ( mtch->is_iterative ) {
		unsigned int key_index = 0;
		string_t *key_item = NULL;
	
		while ( (ok=sieve_coded_stringlist_next_item(mctx->key_list, &key_item)) && 
			key_item != NULL ) 
		{	
			int ret;
			
			if ( mctx->kextract != NULL ) {
				const struct sieve_match_key_extractor *kext = mctx->kextract;
				void *kctx;
				
				if ( (ret=kext->init(&kctx, key_item)) > 0 ) {
					const char *key;
					size_t key_size;
					 			
					while ( (ret=kext->extract_key(kctx, &key, &key_size)) > 0 ) {				
						ret = mtch->match(mctx, value, val_size, key, key_size, key_index);
						
						if ( ret != 0 ) break;
					}
				}  
			} else {
				ret = mtch->match(mctx, value, val_size, str_c(key_item), 
						str_len(key_item), key_index);
			}
			
			if ( ret < 0 ) 
				return ret;

			if ( ret > 0 )
				return TRUE;
	
			key_index++;
		}

		if ( !ok ) 
			return -1;

	} else {
		return mtch->match(mctx, value, strlen(value), NULL, 0, -1);
	}

	return FALSE;
}

int sieve_match_end(struct sieve_match_context *mctx)
{
	const struct sieve_match_type *mtch = mctx->match_type;

	if ( mtch->match_deinit != NULL ) {
		return mtch->match_deinit(mctx);
	}

	return FALSE;
}

/*
 * Reading match operands
 */
 
bool sieve_match_dump_optional_operands
(const struct sieve_dumptime_env *denv, sieve_size_t *address, int *opt_code)
{
	if ( *opt_code != SIEVE_MATCH_OPT_END || 
		sieve_operand_optional_present(denv->sbin, address) ) {
		do {
			if ( !sieve_operand_optional_read(denv->sbin, address, opt_code) ) 
				return FALSE;

			switch ( *opt_code ) {
			case SIEVE_MATCH_OPT_END:
				break;
			case SIEVE_MATCH_OPT_COMPARATOR:
				if ( !sieve_opr_comparator_dump(denv, address) )
					return FALSE;
				break;
			case SIEVE_MATCH_OPT_MATCH_TYPE:
				if ( !sieve_opr_match_type_dump(denv, address) )
					return FALSE;
				break;
			default: 
				return TRUE;
			}
 		} while ( *opt_code != SIEVE_MATCH_OPT_END );
	}
	
	return TRUE;
}

int sieve_match_read_optional_operands
(const struct sieve_runtime_env *renv, sieve_size_t *address, int *opt_code,
	const struct sieve_comparator **cmp_r, const struct sieve_match_type **mtch_r)
{	 
	/* Handle any optional arguments */
	if ( *opt_code != SIEVE_MATCH_OPT_END || 
		sieve_operand_optional_present(renv->sbin, address) ) {
		do {
			if ( !sieve_operand_optional_read(renv->sbin, address, opt_code) ) {
				sieve_runtime_trace_error(renv, "invalid optional operand");
				return SIEVE_EXEC_BIN_CORRUPT;
			}

			switch ( *opt_code ) {
			case SIEVE_MATCH_OPT_END: 
				break;
			case SIEVE_MATCH_OPT_COMPARATOR:
				if ( (*cmp_r = sieve_opr_comparator_read(renv, address)) == NULL ) {
					sieve_runtime_trace_error(renv, "invalid comparator operand");
					return SIEVE_EXEC_BIN_CORRUPT;
				}
				break;
			case SIEVE_MATCH_OPT_MATCH_TYPE:
				if ( (*mtch_r = sieve_opr_match_type_read(renv, address)) == NULL ) {
					sieve_runtime_trace_error(renv, "invalid match type operand");
					return SIEVE_EXEC_BIN_CORRUPT;
				}
				break;
			default:
				return SIEVE_EXEC_OK;
			}
		} while ( *opt_code != SIEVE_MATCH_OPT_END );
	}
	
	return SIEVE_EXEC_OK;
}

