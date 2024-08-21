/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "unichar.h"
#include "str.h"

#include "sieve-common.h"
#include "sieve-code.h"

#include "sieve-ext-variables.h"

#include "ext-enotify-common.h"

/*
 * Encodeurl modifier
 */

static bool
mod_encodeurl_modify(const struct sieve_variables_modifier *modf,
		     string_t *in, string_t **result);

const struct sieve_variables_modifier_def encodeurl_modifier = {
	SIEVE_OBJECT("encodeurl", &encodeurl_operand, 0),
	15,
	mod_encodeurl_modify
};

/*
 * Modifier operand
 */

static const struct sieve_extension_objects ext_enotify_modifiers =
	SIEVE_VARIABLES_DEFINE_MODIFIER(encodeurl_modifier);

const struct sieve_operand_def encodeurl_operand = {
	.name = "modifier",
	.ext_def = &enotify_extension,
	.class = &sieve_variables_modifier_operand_class,
	.interface = &ext_enotify_modifiers
};

/*
 * Modifier implementation
 */

static const char _uri_reserved_lookup[256] = {
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 00
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 10
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1,  // 20
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,  // 30
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 40
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0,  // 50
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 60
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 1,  // 70

	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 80
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 90
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // A0
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // B0
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // C0
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // D0
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // E0
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // F0
};

static bool
mod_encodeurl_modify(const struct sieve_variables_modifier *modf,
		     string_t *in, string_t **result)
{
	size_t max_val_size =
		sieve_variables_get_max_value_size(modf->var_ext);
	const unsigned char *p, *poff, *pend;
	size_t new_size;

	if ( str_len(in) == 0 ) {
		*result = in;
		return TRUE;
	}

	/* allocate new string */
	new_size = str_len(in) + 32;
	if (new_size > max_val_size)
		new_size = max_val_size;
	*result = t_str_new(new_size + 1);

	/* escape string */
	p = str_data(in);
	pend = p + str_len(in);
	poff = p;
	while (p < pend) {
		unsigned int i, n = uni_utf8_char_bytes(*p);

		if (n > 1 || (_uri_reserved_lookup[*p] & 0x01) != 0) {
			str_append_data(*result, poff, p - poff);
			poff = p;

			if (str_len(*result) + 3 * n > max_val_size)
				break;

			str_printfa(*result, "%%%02X", *p);
			for (i = 1; i < n && p < pend; i++) {
				p++;
				poff++;
				str_printfa(*result, "%%%02X", *p);
			}

			poff++;
		} else if ((str_len(*result) + (p - poff) + 1) > max_val_size) {
			break;
		}
		p++;
	}

	str_append_data(*result, poff, p - poff);
	return TRUE;
}


