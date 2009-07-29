/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */
 
/* Match-type ':count' 
 */

#include "lib.h"
#include "str.h"

#include "sieve-common.h"
#include "sieve-ast.h"
#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-match.h"

#include "ext-relational-common.h"

/* 
 * Forward declarations
 */

static void mcht_count_match_init(struct sieve_match_context *mctx);
static int mcht_count_match
	(struct sieve_match_context *mctx, const char *val, size_t val_size, 
		const char *key, size_t key_size, int key_index);
static int mcht_count_match_deinit(struct sieve_match_context *mctx);

/* 
 * Match-type objects
 */
 
const struct sieve_match_type count_match_type = {
	SIEVE_OBJECT("count", &rel_match_type_operand, RELATIONAL_COUNT),
	FALSE, FALSE,
	mcht_relational_validate,
	NULL, NULL, NULL, NULL
};

#define COUNT_MATCH_TYPE(name, rel_match)                     \
const struct sieve_match_type rel_match_count_ ## name = {    \
	SIEVE_OBJECT(                                             \
		"count-" #name, &rel_match_type_operand,              \
		REL_MATCH_INDEX(RELATIONAL_COUNT, rel_match)),        \
	FALSE, FALSE,                                             \
	NULL, NULL,                                               \
	mcht_count_match_init,                                    \
	mcht_count_match,                                         \
	mcht_count_match_deinit                                   \
}
	
COUNT_MATCH_TYPE(gt, REL_MATCH_GREATER);
COUNT_MATCH_TYPE(ge, REL_MATCH_GREATER_EQUAL);
COUNT_MATCH_TYPE(lt, REL_MATCH_LESS);
COUNT_MATCH_TYPE(le, REL_MATCH_LESS_EQUAL);
COUNT_MATCH_TYPE(eq, REL_MATCH_EQUAL);
COUNT_MATCH_TYPE(ne, REL_MATCH_NOT_EQUAL);

/* 
 * Match-type implementation 
 */

struct mcht_count_context {
	unsigned int count;
};

static void mcht_count_match_init(struct sieve_match_context *mctx)
{
	struct mcht_count_context *cctx = p_new(mctx->pool, struct mcht_count_context, 1);

	cctx->count = 0;
	mctx->data = (void *) cctx;
}

static int mcht_count_match
(struct sieve_match_context *mctx, 
	const char *val ATTR_UNUSED, size_t val_size ATTR_UNUSED, 
	const char *key ATTR_UNUSED, size_t key_size ATTR_UNUSED,
	int key_index) 
{
	if ( val == NULL )
		return FALSE;

	/* Count values */
	if ( key_index == -1 ) {
		struct mcht_count_context *cctx = 
			(struct mcht_count_context *) mctx->data;

		cctx->count++;
	}

	return FALSE;
}

static int mcht_count_match_deinit(struct sieve_match_context *mctx)
{
	struct mcht_count_context *cctx =
            (struct mcht_count_context *) mctx->data;
	int key_index;
	string_t *key_item;
    sieve_coded_stringlist_reset(mctx->key_list);
	bool ok = TRUE;

	string_t *value = t_str_new(20);
	str_printfa(value, "%d", cctx->count);

    /* Match to all key values */
    key_index = 0;
    key_item = NULL;
    while ( (ok=sieve_coded_stringlist_next_item(mctx->key_list, &key_item)) 
		&& key_item != NULL )
    {
		int ret = mcht_value_match
			(mctx, str_c(value), str_len(value), str_c(key_item), 
				str_len(key_item), key_index);
        
		if ( ret > 0 )   
			return TRUE;
	
		if ( ret < 0 )
			return ret;

        key_index++;
    }

	return ( ok ? FALSE : -1 );
}




