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

#include "ext-relational-common.h"

/* 
 * Forward declarations
 */

static void mcht_count_match_init(struct sieve_match_context *mctx);
static bool mcht_count_match
	(struct sieve_match_context *mctx, const char *val, size_t val_size, 
		const char *key, size_t key_size, int key_index);
static bool mcht_count_match_deinit(struct sieve_match_context *mctx);

/* 
 * Match-type objects
 */

const struct sieve_match_type count_match_type = {
	"count", FALSE,
	&rel_match_type_operand,
	RELATIONAL_COUNT,
	mcht_relational_validate,
	NULL, NULL, NULL, NULL
};

#define COUNT_MATCH_TYPE(name, rel_match)                   \
const struct sieve_match_type rel_match_count_ ## name = {  \
	"count-" #name, FALSE,                                  \
	&rel_match_type_operand,                                \
	REL_MATCH_INDEX(RELATIONAL_COUNT, rel_match),           \
	NULL, NULL,                                             \
	mcht_count_match_init,                                  \
	mcht_count_match,                                       \
	mcht_count_match_deinit                                 \
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

static void mcht_count_match_init(struct sieve_match_context *mctx)
{
	mctx->data = (void *) 0;
}

static bool mcht_count_match
(struct sieve_match_context *mctx, 
	const char *val ATTR_UNUSED, size_t val_size ATTR_UNUSED, 
	const char *key ATTR_UNUSED, size_t key_size ATTR_UNUSED,
	 int key_index) 
{
	unsigned int val_count = (unsigned int) mctx->data;

	/* Count values */
	if ( key_index == -1 ) {
		val_count++;
		mctx->data = (void *) val_count;	
	}

	return FALSE;
}

static bool mcht_count_match_deinit(struct sieve_match_context *mctx)
{
	unsigned int val_count = (unsigned int) mctx->data;
	int key_index;
	string_t *key_item;
    sieve_coded_stringlist_reset(mctx->key_list);

	string_t *value = t_str_new(20);
	str_printfa(value, "%d", val_count);
	
    /* Match to all key values */
    key_index = 0;
    key_item = NULL;
    while ( sieve_coded_stringlist_next_item(mctx->key_list, &key_item) &&
        key_item != NULL )
    {
        if ( mcht_value_match
			(mctx, str_c(value), str_len(value), str_c(key_item), 
			str_len(key_item), key_index) )
            return TRUE;

        key_index++;
    }

	return FALSE;
}




