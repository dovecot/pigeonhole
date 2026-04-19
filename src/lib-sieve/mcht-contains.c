/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

/* Match-type ':contains'
 */

#include "lib.h"

#include "sieve-match-types.h"
#include "sieve-comparators.h"
#include "sieve-interpreter.h"
#include "sieve-match.h"

#include <string.h>
#include <stdio.h>

/*
 * Forward declarations
 */

static int mcht_contains_match_key
	(struct sieve_match_context *mctx, const char *val, size_t val_size,
		const char *key, size_t key_size);

/*
 * Match-type object
 */

const struct sieve_match_type_def contains_match_type = {
	SIEVE_OBJECT("contains",
		&match_type_operand, SIEVE_MATCH_TYPE_CONTAINS),
	.validate_context = sieve_match_substring_validate_context,
	.match_key = mcht_contains_match_key
};

/*
 * Match-type implementation
 */

/* FIXME: Naive substring match implementation. Should switch to more
 * efficient algorithm if large values need to be searched (e.g. message body).
 *
 * The inner loop polls the interpreter CPU time limit periodically so that a
 * single O(N*M) match on a large value cannot run for many times the
 * configured sieve_max_cpu_time (which is otherwise only checked between
 * bytecode operations).
 */
#define SIEVE_CONTAINS_CPU_CHECK_INTERVAL 4096

static int mcht_contains_match_key
(struct sieve_match_context *mctx, const char *val, size_t val_size,
	const char *key, size_t key_size)
{
	const struct sieve_comparator *cmp = mctx->comparator;
	const char *vend = (const char *) val + val_size;
	const char *kend = (const char *) key + key_size;
	const char *vp = val;
	const char *kp = key;
	unsigned int counter = 0;

	if ( val_size == 0 )
		return ( key_size == 0 ? 1 : 0 );

	if ( cmp->def == NULL || cmp->def->char_match == NULL )
		return 0;

	while ( (vp < vend) && (kp < kend) ) {
		if ( !cmp->def->char_match(cmp, &vp, vend, &kp, kend) )
			vp++;

		if ( ++counter >= SIEVE_CONTAINS_CPU_CHECK_INTERVAL ) {
			counter = 0;
			if ( sieve_runtime_cpu_limit_exceeded(mctx->runenv) ) {
				sieve_runtime_error(
					mctx->runenv, NULL,
					"execution exceeded CPU time limit");
				mctx->exec_status =
					SIEVE_EXEC_RESOURCE_LIMIT;
				return -1;
			}
		}
	}

	return ( kp == kend ? 1 : 0 );
}


