/* Copyright (c) 2024 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "settings.h"
#include "settings-parser.h"

#include "ext-spamvirustest-settings.h"

#include <ctype.h>

static bool
ext_spamtest_settings_check(void *_set, pool_t pool, const char **error_r);
static bool
ext_virustest_settings_check(void *_set, pool_t pool, const char **error_r);

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("sieve_spamtest_"#name, name, \
				     struct ext_spamvirustest_settings)

static const struct setting_define ext_spamtest_setting_defines[] = {
	DEF(STR, status_header),
	DEF(STR, status_type),

	DEF(STR, score_max_header),
	DEF(STR, score_max_value),

	{ .type = SET_STRLIST, .key = "sieve_spamtest_text_value",
	  .offset = offsetof(struct ext_spamvirustest_settings,
			     text_value) },

	SETTING_DEFINE_LIST_END,
};

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("sieve_virustest_"#name, name, \
				     struct ext_spamvirustest_settings)

static const struct setting_define ext_virustest_setting_defines[] = {
	DEF(STR, status_header),
	DEF(STR, status_type),

	DEF(STR, score_max_header),
	DEF(STR, score_max_value),

	{ .type = SET_STRLIST, .key = "sieve_virustest_text_value",
	  .offset = offsetof(struct ext_spamvirustest_settings,
			     text_value) },

	SETTING_DEFINE_LIST_END,
};

static const struct ext_spamvirustest_settings ext_spamvirustest_default_settings = {
	.status_header = "",
	.status_type = "",

	.score_max_header = "",
	.score_max_value = "",

	.text_value = ARRAY_INIT,
};

const struct setting_parser_info ext_spamtest_setting_parser_info = {
	.name = "sieve_spamtest",

	.defines = ext_spamtest_setting_defines,
	.defaults = &ext_spamvirustest_default_settings,

	.struct_size = sizeof(struct ext_spamvirustest_settings),

	.check_func = ext_spamtest_settings_check,

	.pool_offset1 = 1 + offsetof(struct ext_spamvirustest_settings, pool),
};

const struct setting_parser_info ext_virustest_setting_parser_info = {
	.name = "sieve_virustest",

	.defines = ext_virustest_setting_defines,
	.defaults = &ext_spamvirustest_default_settings,

	.struct_size = sizeof(struct ext_spamvirustest_settings),

	.check_func = ext_virustest_settings_check,

	.pool_offset1 = 1 + offsetof(struct ext_spamvirustest_settings, pool),
};

/* <settings checks> */
bool ext_spamvirustest_parse_decimal_value(const char *str_value,
					   float *value_r, const char **error_r)
{
	const char *p = str_value;
	float value;
	float sign = 1;
	int digits;

	if (*p == '\0') {
		*error_r = "empty value";
		return FALSE;
	}

	if (*p == '+' || *p == '-') {
		if (*p == '-')
			sign = -1;
		p++;
	}

	value = 0;
	digits = 0;
	while (i_isdigit(*p)) {
		value = value*10 + (*p-'0');
		if (digits++ > 4) {
			*error_r = t_strdup_printf(
				"Decimal value has too many digits before radix point: %s",
				str_value);
			return FALSE;
		}
		p++;
	}

	if (*p == '.' || *p == ',') {
		float radix = .1;
		p++;

		digits = 0;
		while (i_isdigit(*p)) {
			value = value + (*p-'0')*radix;

			if (digits++ > 4) {
				*error_r = t_strdup_printf(
					"Decimal value has too many digits after radix point: %s",
					str_value);
				return FALSE;
			}
			radix /= 10;
			p++;
		}
	}

	if (*p != '\0') {
		*error_r = t_strdup_printf(
			"Invalid decimal point value: %s", str_value);
		return FALSE;
	}

	*value_r = value * sign;
	return TRUE;
}

static bool
ext_spamvirustest_settings_check(void *_set, bool virustest,
				 pool_t pool ATTR_UNUSED, const char **error_r)
{
	struct ext_spamvirustest_settings *set = _set;
	const char *ext_name = (virustest ? "virustest" : "spamtest");
	const char *error;

	if (*set->status_header == '\0')
		return TRUE;

	if (*set->status_type == '\0' ||
	    strcmp(set->status_type, "score") == 0)
		set->parsed.status_type = EXT_SPAMVIRUSTEST_STATUS_TYPE_SCORE;
	else if (strcmp(set->status_type, "strlen") == 0)
		set->parsed.status_type = EXT_SPAMVIRUSTEST_STATUS_TYPE_STRLEN;
	else if (strcmp(set->status_type, "text") == 0)
		set->parsed.status_type = EXT_SPAMVIRUSTEST_STATUS_TYPE_TEXT;
	else {
		*error_r = t_strdup_printf("Invalid status type '%s'",
					   set->status_type);
		return FALSE;
	}

	if (set->parsed.status_type != EXT_SPAMVIRUSTEST_STATUS_TYPE_TEXT) {
		if (*set->score_max_header != '\0' &&
		    *set->score_max_value != '\0') {
			*error_r = t_strdup_printf(
				"sieve_%s_score_max_header and sieve_%s_score_max_value "
				"cannot both be configured",
				ext_name, ext_name);
			return FALSE;
		}
		if (*set->score_max_header == '\0' &&
		    *set->score_max_value == '\0') {
			*error_r = t_strdup_printf(
				"None of sieve_%s_score_max_header or "
				"sieve_%s_score_max_value is configured",
				ext_name, ext_name);
			return FALSE;
		}
		if (*set->score_max_value != '\0' &&
		    !ext_spamvirustest_parse_decimal_value(
			set->score_max_value, &set->parsed.score_max_value,
			&error)) {
			*error_r = t_strdup_printf(
				"Invalid max score value specification "
				"'%s': %s", set->score_max_value, error);
			return FALSE;
		}
	} else {
		const char *const *tvalues;
		unsigned int tvalues_count, i;
		unsigned int tv_index_max = (virustest ? 5 : 10);

		tvalues = array_get(&set->text_value, &tvalues_count);
		i_assert(tvalues_count % 2 == 0);
		for (i = 0; i < tvalues_count; i += 2) {
			unsigned int tv_index;

			if (str_to_uint(tvalues[i], &tv_index) < 0) {
				*error_r = t_strdup_printf(
					"Invalid text value index '%s'",
					tvalues[i]);
				return FALSE;
			}
			if (tv_index > tv_index_max) {
				*error_r = t_strdup_printf(
					"Text value index out of range "
					"(%u > %u)", tv_index, tv_index_max);
				return FALSE;
			}
			set->parsed.text_values[tv_index] = tvalues[i + 1]; 
		}
		set->parsed.score_max_value = 1;
	}

	return TRUE;
}

static bool
ext_spamtest_settings_check(void *_set, pool_t pool ATTR_UNUSED,
			    const char **error_r)
{
	return ext_spamvirustest_settings_check(_set, FALSE, pool, error_r);
}

static bool
ext_virustest_settings_check(void *_set, pool_t pool ATTR_UNUSED,
			     const char **error_r)
{
	return ext_spamvirustest_settings_check(_set, TRUE, pool, error_r);
}
/* </settings checks> */

