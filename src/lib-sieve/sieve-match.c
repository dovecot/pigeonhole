/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "mempool.h"
#include "hash.h"
#include "array.h"
#include "str-sanitize.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-stringlist.h"
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
(const struct sieve_runtime_env *renv,
	const struct sieve_match_type *mcht, 
	const struct sieve_comparator *cmp)
{
	struct sieve_match_context *mctx;
	pool_t pool;

	/* Reject unimplemented match-type */
	if ( mcht->def == NULL || (mcht->def->match == NULL && 
			mcht->def->match_keys == NULL && mcht->def->match_key == NULL) )
			return NULL;

	/* Create match context */
	pool = pool_alloconly_create("sieve_match_context", 1024);
	mctx = p_new(pool, struct sieve_match_context, 1);  
	mctx->pool = pool;
	mctx->runenv = renv;
	mctx->match_type = mcht;
	mctx->comparator = cmp;
	mctx->trace = sieve_runtime_trace_active(renv, SIEVE_TRLVL_MATCHING);

	/* Trace */
	if ( mctx->trace ) {
		sieve_runtime_trace_descend(renv);
		sieve_runtime_trace(renv, 0,
			"starting `:%s' match with `%s' comparator:",
			sieve_match_type_name(mcht), sieve_comparator_name(cmp));
	}

	/* Initialize match type */
	if ( mcht->def != NULL && mcht->def->match_init != NULL ) {
		mcht->def->match_init(mctx);
	}

	return mctx;
}

int sieve_match_value
(struct sieve_match_context *mctx, const char *value, size_t value_size,
	struct sieve_stringlist *key_list)
{
	const struct sieve_match_type *mcht = mctx->match_type;
	const struct sieve_runtime_env *renv = mctx->runenv;
	int result = 0;

	if ( mctx->trace ) {
		sieve_runtime_trace(renv, 0,
			"matching value `%s'", str_sanitize(value, 80));
	}

	/* Match to key values */
	
	sieve_stringlist_reset(key_list);

	if ( mctx->trace )
		sieve_stringlist_set_trace(key_list, TRUE);

	sieve_runtime_trace_descend(renv);

	if ( mcht->def->match_keys != NULL ) {
		/* Call match-type's own key match handler */
		result = mcht->def->match_keys(mctx, value, value_size, key_list);
	} else {
		string_t *key_item = NULL;
		int ret;

		/* Default key match loop */
		while ( result == 0 && 
			(ret=sieve_stringlist_next_item(key_list, &key_item)) > 0 ) {				
			T_BEGIN {
				result = mcht->def->match_key
					(mctx, value, value_size, str_c(key_item), str_len(key_item));

				if ( mctx->trace ) {
					sieve_runtime_trace(renv, 0,
						"with key `%s' => %d", str_sanitize(str_c(key_item), 80),
						result);
				}
			} T_END;
		}

		if ( ret < 0 ) result = -1;
	}

	sieve_runtime_trace_ascend(renv);

	if ( mctx->status < 0 || result < 0 )
		mctx->status = -1;
	else 
		mctx->status = ( mctx->status > result ? mctx->status : result );
	return result;
}

int sieve_match_end(struct sieve_match_context **mctx)
{
	const struct sieve_match_type *mcht = (*mctx)->match_type;
	const struct sieve_runtime_env *renv = (*mctx)->runenv;
	int result = (*mctx)->status;

	if ( mcht->def != NULL && mcht->def->match_deinit != NULL )
		mcht->def->match_deinit(*mctx);

	pool_unref(&(*mctx)->pool);

	sieve_runtime_trace(renv, SIEVE_TRLVL_MATCHING,
		"finishing match with result: %s", 
		( result > 0 ? "matched" : ( result < 0 ? "error" : "not matched" ) ));
	sieve_runtime_trace_ascend(renv);

	return result;
}

int sieve_match
(const struct sieve_runtime_env *renv,
	const struct sieve_match_type *mcht, 
	const struct sieve_comparator *cmp, 
	struct sieve_stringlist *value_list,
	struct sieve_stringlist *key_list)
{
	struct sieve_match_context *mctx;
	string_t *value_item = NULL;
	int result, ret;

	if ( (mctx=sieve_match_begin(renv, mcht, cmp)) == NULL )
		return 0;

	/* Match value to keys */

	sieve_stringlist_reset(value_list);

	if ( mctx->trace )
		sieve_stringlist_set_trace(value_list, TRUE);

	if ( mcht->def->match != NULL ) {
		/* Call match-type's match handler */
		result = mctx->status = mcht->def->match(mctx, value_list, key_list); 

	} else {
		/* Default value match loop */

		result = 0;
		while ( result == 0 && 
			(ret=sieve_stringlist_next_item(value_list, &value_item)) > 0 ) {

			result = sieve_match_value
				(mctx, str_c(value_item), str_len(value_item), key_list);
		}

		if ( ret < 0 ) result = -1;
	}

	(void)sieve_match_end(&mctx);
	return result;
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

