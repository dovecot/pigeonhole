/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file 
 */
 
/* Match-type ':is': 
 */

#include "lib.h"

#include "sieve-match-types.h"
#include "sieve-comparators.h"
#include "sieve-match.h"

#include <string.h>
#include <stdio.h>

/* 
 * Forward declarations 
 */

static int mcht_is_match
	(struct sieve_match_context *mctx, const char *val, size_t val_size, 
		const char *key, size_t key_size, int key_index);

/* 
 * Match-type object 
 */

const struct sieve_match_type is_match_type = {
	SIEVE_OBJECT("is", &match_type_operand, SIEVE_MATCH_TYPE_IS),
	TRUE, TRUE,
	NULL, NULL, NULL,
	mcht_is_match,
	NULL
};

/*
 * Match-type implementation
 */

static int mcht_is_match
(struct sieve_match_context *mctx ATTR_UNUSED, 
	const char *val, size_t val_size, 
	const char *key, size_t key_size, int key_index ATTR_UNUSED)
{
	if ( (val == NULL || val_size == 0) ) 
		return ( key_size == 0 );

	printf ("VAL '%s' KEY '%s'\n", val, key);
	
	if ( mctx->comparator->compare != NULL )
		return (mctx->comparator->compare(mctx->comparator, 
			val, val_size, key, key_size) == 0);

	return FALSE;
}

