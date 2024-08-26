/* Copyright (c) 2024 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "settings.h"
#include "settings-parser.h"

#include "ext-vnd-report-settings.h"

static bool
ext_report_settings_check(void *_set, pool_t pool, const char **error_r);

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("sieve_report_"#name, name, \
				     struct ext_report_settings)

static const struct setting_define ext_report_setting_defines[] = {
	DEF(STR, from),

	SETTING_DEFINE_LIST_END,
};

static const struct ext_report_settings ext_report_default_settings = {
	.from = "",
};

const struct setting_parser_info ext_report_setting_parser_info = {
	.name = "sieve_report",

	.defines = ext_report_setting_defines,
	.defaults = &ext_report_default_settings,

	.struct_size = sizeof(struct ext_report_settings),

	.check_func = ext_report_settings_check,

	.pool_offset1 = 1 + offsetof(struct ext_report_settings, pool),
};

/* <settings checks> */
static bool
ext_report_settings_check(void *_set, pool_t pool, const char **error_r)
{
	struct ext_report_settings *set = _set;

	if (!sieve_address_source_parse(pool, set->from, &set->parsed.from)) {
		*error_r = t_strdup_printf("sieve_report_from: "
					   "Invalid address source '%s'",
					   set->from);
		return FALSE;
	}

	return TRUE;
}
/* </settings checks> */
