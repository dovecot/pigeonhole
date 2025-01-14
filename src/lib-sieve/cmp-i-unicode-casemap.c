/* Copyright (c) 2025 Pigeonhole authors, see the included COPYING file
 */

/* Comparator 'i;unicode-casemap':
 *
 */

#include "lib.h"
#include "unichar.h"

#include "sieve-common.h"
#include "sieve-comparators.h"

/*
 * Comparator implementation
 */

static int
cmp_i_unicode_casemap_compare(const struct sieve_comparator *cmp ATTR_UNUSED,
			      const char *val1, size_t val1_size, const char *val2,
			      size_t val2_size)
{
	string_t *value_a = t_str_new(val1_size);
	string_t *value_b = t_str_new(val2_size);

	uni_utf8_to_decomposed_titlecase(val1, val1_size, value_a);
	uni_utf8_to_decomposed_titlecase(val2, val2_size, value_b);

	val1 = str_c(value_a);
	val2 = str_c(value_b);

	return strcmp(val1, val2);
}

static bool
cmp_i_unicode_casemap_char_match(const struct sieve_comparator *cmp ATTR_UNUSED,
				 const char **val, const char *val_end,
				 const char **key, const char *key_end)
{
	const char *val_begin = *val;
	const char *key_begin = *key;

	while (*val < val_end && *key < key_end) {
		unsigned int val_len = uni_utf8_char_bytes((unsigned char)**val);
		unsigned int key_len = uni_utf8_char_bytes((unsigned char)**key);

		unichar_t val_chr, key_chr;
		uni_utf8_get_char(*val, &val_chr);
		uni_utf8_get_char(*key, &key_chr);

		/* normalize */
		val_chr = uni_ucs4_to_titlecase(val_chr);
		key_chr = uni_ucs4_to_titlecase(key_chr);

		if (val_chr != key_chr)
			break;
		(*val) += val_len;
		(*key) += key_len;
	}

	i_assert(*val <= val_end);
	i_assert(*key <= key_end);

	if (*key < key_end) {
		/* Reset */
		*val = val_begin;
		*key = key_begin;

		return FALSE;
	}

	return TRUE;
}

static bool
cmp_i_unicode_casemap_char_skip(const struct sieve_comparator *cmp ATTR_UNUSED,
				const char **val, const char *val_end)
{
	if (*val >= val_end)
		return FALSE;
	unsigned int len = uni_utf8_char_bytes(**val);
	(*val) += len;
	return TRUE;
}

/*
 * Comparator object
 */

const struct sieve_comparator_def i_unicode_casemap_comparator = {
	SIEVE_OBJECT("i;unicode-casemap",
		&comparator_operand, SIEVE_COMPARATOR_I_UNICODE_CASEMAP),
	.flags =
		SIEVE_COMPARATOR_FLAG_EQUALITY |
		SIEVE_COMPARATOR_FLAG_SUBSTRING_MATCH |
		SIEVE_COMPARATOR_FLAG_PREFIX_MATCH,
	.compare = cmp_i_unicode_casemap_compare,
	.char_match = cmp_i_unicode_casemap_char_match,
	.char_skip = cmp_i_unicode_casemap_char_skip,
};
