/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "mempool.h"
#include "hash.h"
#include "array.h"
#include "str-sanitize.h"

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
#include "sieve-runtime-trace.h"

#include "sieve-match.h"

/*
 * Matching implementation
 */

struct sieve_match_context *sieve_match_begin
(const struct sieve_runtime_env *renv, const struct sieve_match_type *mcht, 
	const struct sieve_comparator *cmp, 
	const struct sieve_match_key_extractor *kextract,
	struct sieve_coded_stringlist *key_list)
{
	struct sieve_match_context *mctx;
	pool_t pool;

	pool = pool_alloconly_create("sieve_match_context", 1024);
	mctx = p_new(pool, struct sieve_match_context, 1);  

	mctx->pool = pool;
	mctx->runenv = renv;
	mctx->match_type = mcht;
	mctx->comparator = cmp;
	mctx->kextract = kextract;
	mctx->key_list = key_list;

	sieve_runtime_trace(renv, SIEVE_TRLVL_MATCHING,
		"  starting `:%s' match with `%s' comparator:", sieve_match_type_name(mcht),
		sieve_comparator_name(cmp));

	if ( mcht->def != NULL && mcht->def->match_init != NULL ) {
		mcht->def->match_init(mctx);
	}

	return mctx;
}

int sieve_match_value
(struct sieve_match_context *mctx, const char *value, size_t val_size)
{
	const struct sieve_runtime_env *renv = mctx->runenv;
	const struct sieve_match_type *mcht = mctx->match_type;
	sieve_coded_stringlist_reset(mctx->key_list);
	bool trace = sieve_runtime_trace_active(renv, SIEVE_TRLVL_MATCHING);
	bool ok = TRUE;
	int ret = 0;

	/* Reject unimplemented match-type */
	if ( mcht->def == NULL || mcht->def->match == NULL ) {
		mctx->status = FALSE;
		return FALSE;
	}

	if ( trace ) {
		sieve_runtime_trace(renv, 0,
			"  matching value `%s'", str_sanitize(value, 80));
	}

	/* Match to all key values */
	if ( mcht->def->is_iterative ) {
		unsigned int key_index = 0;
		string_t *key_item = NULL;
	
		while ( (ok=sieve_coded_stringlist_next_item(mctx->key_list, &key_item)) 
			&& key_item != NULL ) {				
			T_BEGIN {
				if ( mctx->kextract != NULL && mcht->def->allow_key_extract ) {
					const struct sieve_match_key_extractor *kext = mctx->kextract;
					void *kctx;
				
					if ( (ret=kext->init(&kctx, key_item)) > 0 ) {
						const char *key;
						size_t key_size;
					 			
						while ( (ret=kext->extract_key(kctx, &key, &key_size)) > 0 ) {				
							ret = mcht->def->match
								(mctx, value, val_size, key, key_size, key_index);
						
							if ( trace ) {
								sieve_runtime_trace(renv, 0,
									"    with key `%s' => %d", str_sanitize(key, 80), ret);
							}

							if ( ret != 0 ) break;
						}
					}  
				} else {
					ret = mcht->def->match(mctx, value, val_size, str_c(key_item), 
							str_len(key_item), key_index);

					if ( trace ) {
						sieve_runtime_trace(renv, 0,
							"    with key `%s' => %d", str_sanitize(str_c(key_item), 80), ret);
					}
				}
			} T_END;
			
			if ( ret != 0 )
				break;
	
			key_index++;
		}

		if ( !ok ) ret = -1;

	} else {
		T_BEGIN {
			ret = mcht->def->match(mctx, value, val_size, NULL, 0, -1);
		} T_END;
	}

	mctx->status = ret;
	return ret;
}

int sieve_match_end(struct sieve_match_context **mctx)
{
	const struct sieve_runtime_env *renv = (*mctx)->runenv;
	const struct sieve_match_type *mcht = (*mctx)->match_type;
	int status = (*mctx)->status;
	int ret = FALSE;

	if ( mcht->def != NULL && mcht->def->match_deinit != NULL ) {
		ret = mcht->def->match_deinit(*mctx);
	}

	pool_unref(&(*mctx)->pool);
	*mctx = NULL;

	if ( ret < 0 || status < 0 )
		status = ( ret <= status ? ret : status );
	else
		status = status || ret;

	sieve_runtime_trace(renv, SIEVE_TRLVL_MATCHING,
		"  finishing match with result: %s", 
		( status > 0 ? "true" : ( status < 0 ? "error" : "false" ) ));

	return ret;
}

/*
 * Reading match operands
 */
 
int sieve_match_opr_optional_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address, int *opt_code)
{
	bool opok = TRUE;

	while ( opok ) {
		int ret;

		if ( (ret=sieve_opr_optional_dump(denv, address, opt_code)) <= 0 )
			return ret;

		switch ( *opt_code ) {
		case SIEVE_MATCH_OPT_COMPARATOR:
			opok = sieve_opr_comparator_dump(denv, address);
			break;
		case SIEVE_MATCH_OPT_MATCH_TYPE:
			opok = sieve_opr_match_type_dump(denv, address);
			break;
		default:
			return 1;
		}
	}

	return -1;
}

int sieve_match_opr_optional_read
(const struct sieve_runtime_env *renv, sieve_size_t *address, int *opt_code,
	struct sieve_comparator *cmp, struct sieve_match_type *mcht)
{
	bool opok = TRUE;

	while ( opok ) {
		int ret;

		if ( (ret=sieve_opr_optional_read(renv, address, opt_code)) <= 0 )
			return ret;

		switch ( *opt_code ) {
		case SIEVE_MATCH_OPT_COMPARATOR:
			opok = sieve_opr_comparator_read(renv, address, cmp);
			break;
		case SIEVE_MATCH_OPT_MATCH_TYPE:
			opok = sieve_opr_match_type_read(renv, address, mcht);
			break;
		default:
			return 1;
		}
	}

	return -1;
}

