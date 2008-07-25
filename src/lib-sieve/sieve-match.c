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

struct sieve_match_context *sieve_match_begin
(struct sieve_interpreter *interp, const struct sieve_match_type *mtch, 
	const struct sieve_comparator *cmp, struct sieve_coded_stringlist *key_list)
{
	struct sieve_match_context *mctx = t_new(struct sieve_match_context, 1);  

	mctx->interp = interp;
	mctx->match_type = mtch;
	mctx->comparator = cmp;
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
			int ret = mtch->match(mctx, value, val_size, str_c(key_item), 
				str_len(key_item), key_index);

			if ( ret < 0 ) return ret;

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

