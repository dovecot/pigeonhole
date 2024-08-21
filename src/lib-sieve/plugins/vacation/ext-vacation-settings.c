/* Copyright (c) 2024 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "settings.h"
#include "settings-parser.h"

#include "ext-vacation-settings.h"

static bool
ext_vacation_settings_check(void *_set, pool_t pool ATTR_UNUSED,
			  const char **error_r);

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("sieve_vacation_"#name, name, \
				     struct ext_vacation_settings)

static const struct setting_define ext_vacation_setting_defines[] = {
	DEF(TIME, min_period),
	DEF(TIME, max_period),
	DEF(TIME, default_period),

	DEF(STR, default_subject),
	DEF(STR, default_subject_template),

	DEF(BOOL, use_original_recipient),
	DEF(BOOL, dont_check_recipient),
	DEF(BOOL, send_from_recipient),
	DEF(BOOL, to_header_ignore_envelope),

	SETTING_DEFINE_LIST_END,
};

static const struct ext_vacation_settings ext_vacation_default_settings = {
	.min_period = (24*60*60),
	.max_period = 0,
	.default_period = (7*24*60*60),
	.default_subject = "",
	.default_subject_template = "",
	.use_original_recipient = FALSE,
	.dont_check_recipient = FALSE,
	.send_from_recipient = FALSE,
	.to_header_ignore_envelope = FALSE,
};

const struct setting_parser_info ext_vacation_setting_parser_info = {
	.name = "sieve_vacation",

	.defines = ext_vacation_setting_defines,
	.defaults = &ext_vacation_default_settings,

	.struct_size = sizeof(struct ext_vacation_settings),

	.check_func = ext_vacation_settings_check,

	.pool_offset1 = 1 + offsetof(struct ext_vacation_settings, pool),
};

/* <settings checks> */
static bool
ext_vacation_settings_check(void *_set, pool_t pool ATTR_UNUSED,
			  const char **error_r)
{
	struct ext_vacation_settings *set = _set;

	if (set->max_period > 0 &&
	    (set->min_period > set->max_period ||
	     set->default_period < set->min_period ||
	     set->default_period > set->max_period)) {
		*error_r = "Violated sieve_vacation_min_period < "
			  "sieve_vacation_default_period < "
			  "sieve_vacation_max_period";
		return FALSE;
	}

	return TRUE;
}
/* </settings checks> */

