/* Copyright (c) 2024 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "settings.h"
#include "settings-parser.h"

#include "ext-variables-limits.h"
#include "ext-variables-settings.h"

static bool
ext_variables_settings_check(void *_set, pool_t pool ATTR_UNUSED,
			     const char **error_r);

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("sieve_variables_"#name, name, \
				     struct ext_variables_settings)

static const struct setting_define ext_variables_setting_defines[] = {
	DEF(UINT, max_scope_size),
	DEF(SIZE, max_variable_size),

	SETTING_DEFINE_LIST_END,
};

static const struct ext_variables_settings ext_variables_default_settings = {
	.max_scope_size = 255,
	.max_variable_size = (4 * 1024),
};

const struct setting_parser_info ext_variables_setting_parser_info = {
	.name = "sieve_variables",

	.defines = ext_variables_setting_defines,
	.defaults = &ext_variables_default_settings,

	.struct_size = sizeof(struct ext_variables_settings),

	.check_func = ext_variables_settings_check,

	.pool_offset1 = 1 + offsetof(struct ext_variables_settings, pool),
};

/* <settings checks> */
static bool
ext_variables_settings_check(void *_set, pool_t pool ATTR_UNUSED,
			     const char **error_r)
{
	struct ext_variables_settings *set = _set;

	if (set->max_scope_size < EXT_VARIABLES_REQUIRED_MAX_SCOPE_SIZE) {
		*error_r = t_strdup_printf(
			  "Setting sieve_variables_max_scope_size "
			  "is lower than required by standards "
			  "(>= %llu items)",
			  (unsigned long long)
				EXT_VARIABLES_REQUIRED_MAX_SCOPE_SIZE);
		return FALSE;
	}
	if (set->max_variable_size < EXT_VARIABLES_REQUIRED_MAX_VARIABLE_SIZE) {
		*error_r = t_strdup_printf(
			  "Setting sieve_variables_max_variable_size "
			  "is lower than required by standards "
			  "(>= %zu bytes)",
			  (size_t)EXT_VARIABLES_REQUIRED_MAX_VARIABLE_SIZE);
		return FALSE;
	}

	return TRUE;
}
/* </settings checks> */

