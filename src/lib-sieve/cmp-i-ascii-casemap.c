/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

/* Comparator 'i;ascii-casemap':
 *
 */

#include "lib.h"

#include "sieve-common.h"
#include "sieve-comparators.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>

/*
 * Forward declarations
 */

static int cmp_i_ascii_casemap_compare
	(const struct sieve_comparator *cmp,
		const char *val1, size_t val1_size, const char *val2, size_t val2_size);
static bool cmp_i_ascii_casemap_char_match
	(const struct sieve_comparator *cmp, const char **val1, const char *val1_end,
		const char **val2, const char *val2_end);

/*
 * Comparator object
 */

const struct sieve_comparator_def i_ascii_casemap_comparator = {
	SIEVE_OBJECT("i;ascii-casemap",
		&comparator_operand, SIEVE_COMPARATOR_I_ASCII_CASEMAP),
	.flags =
		SIEVE_COMPARATOR_FLAG_ORDERING |
		SIEVE_COMPARATOR_FLAG_EQUALITY |
		SIEVE_COMPARATOR_FLAG_SUBSTRING_MATCH |
		SIEVE_COMPARATOR_FLAG_PREFIX_MATCH,
	.compare = cmp_i_ascii_casemap_compare,
	.char_match = cmp_i_ascii_casemap_char_match,
	.char_skip = sieve_comparator_octet_skip
};

/*
 * Comparator implementation
 */

static int cmp_i_ascii_casemap_compare(
	const struct sieve_comparator *cmp ATTR_UNUSED,
	const char *val1, size_t val1_size, const char *val2, size_t val2_size)
{
	size_t size = I_MIN(val1_size, val2_size);
	int ret = strncasecmp(val1, val2, size);
	return ret != 0 ? ret : (int)val1_size - (int)val2_size;
}

static bool cmp_i_ascii_casemap_char_match
	(const struct sieve_comparator *cmp ATTR_UNUSED,
		const char **val, const char *val_end,
		const char **key, const char *key_end)
{
	const char *val_begin = *val;
	const char *key_begin = *key;

	while ( i_tolower(**val) == i_tolower(**key) &&
		*val < val_end && *key < key_end ) {
		(*val)++;
		(*key)++;
	}

	if ( *key < key_end ) {
		/* Reset */
		*val = val_begin;
		*key = key_begin;

		return FALSE;
	}

	return TRUE;
}


