/* Copyright (c) 2024 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "settings.h"
#include "settings-parser.h"

#include "rfc2822.h"
#include "ext-editheader-limits.h"
#include "ext-editheader-settings.h"

static bool
ext_editheader_header_settings_check(void *_set, pool_t pool,
				     const char **error_r);
static bool
ext_editheader_settings_check(void *_set, pool_t pool, const char **error_r);

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("sieve_editheader_header_"#name, name, \
				     struct ext_editheader_header_settings)

static const struct setting_define ext_editheader_header_setting_defines[] = {
	DEF(STR, name),
	DEF(BOOL, forbid_add),
	DEF(BOOL, forbid_delete),

	SETTING_DEFINE_LIST_END,
};

static const struct ext_editheader_header_settings
ext_editheader_header_default_settings = {
	.name = "",
	.forbid_add = FALSE,
	.forbid_delete = FALSE,
};

const struct setting_parser_info ext_editheader_header_setting_parser_info = {
	.name = "sieve_editheader_header",

	.defines = ext_editheader_header_setting_defines,
	.defaults = &ext_editheader_header_default_settings,

	.struct_size = sizeof(struct ext_editheader_header_settings),

	.check_func = ext_editheader_header_settings_check,

	.pool_offset1 = 1 + offsetof(struct ext_editheader_header_settings, pool),
};

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("sieve_editheader_"#name, name, \
				     struct ext_editheader_settings)

static const struct setting_define ext_editheader_setting_defines[] = {
	DEF(SIZE, max_header_size),

	{ .type = SET_FILTER_ARRAY,
	  .key = "sieve_editheader_header",
	  .filter_array_field_name = "sieve_editheader_header_name",
	  .offset = offsetof(struct ext_editheader_settings, headers), },

	SETTING_DEFINE_LIST_END,
};

static const struct ext_editheader_settings ext_editheader_default_settings = {
	.max_header_size = EXT_EDITHEADER_DEFAULT_MAX_HEADER_SIZE,
	.headers = ARRAY_INIT,
};

const struct setting_parser_info ext_editheader_setting_parser_info = {
	.name = "sieve_editheader",

	.defines = ext_editheader_setting_defines,
	.defaults = &ext_editheader_default_settings,

	.struct_size = sizeof(struct ext_editheader_settings),

	.check_func = ext_editheader_settings_check,

	.pool_offset1 = 1 + offsetof(struct ext_editheader_settings, pool),
};

/* <settings checks> */
static bool
ext_editheader_header_settings_check(void *_set, pool_t pool ATTR_UNUSED,
				     const char **error_r)
{
	struct ext_editheader_header_settings *set = _set;

	if (!rfc2822_header_field_name_verify(set->name, strlen(set->name))) {
		*error_r = t_strdup_printf(
			"sieve_editheader_header_name: "
			"Invalid header field name '%s'", set->name);
		return FALSE;
	}

	return TRUE;
}

static bool
ext_editheader_settings_check(void *_set, pool_t pool ATTR_UNUSED,
			     const char **error_r)
{
	struct ext_editheader_settings *set = _set;

	if (set->max_header_size < EXT_EDITHEADER_MINIMUM_MAX_HEADER_SIZE) {
		*error_r = t_strdup_printf(
			"sieve_editheader_max_header_size: "
			"Value (=%"PRIuUOFF_T") is less than the minimum "
			"(=%"PRIuUOFF_T") ",
			set->max_header_size,
			(size_t)EXT_EDITHEADER_MINIMUM_MAX_HEADER_SIZE);
		return FALSE;
	}

	return TRUE;
}
/* </settings checks> */

