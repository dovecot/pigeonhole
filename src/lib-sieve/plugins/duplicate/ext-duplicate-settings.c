/* Copyright (c) 2024 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "settings.h"
#include "settings-parser.h"

#include "ext-duplicate-settings.h"

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("sieve_duplicate_"#name, name, \
				     struct ext_duplicate_settings)

static const struct setting_define ext_duplicate_setting_defines[] = {
	DEF(TIME, default_period),
	DEF(TIME, max_period),

	SETTING_DEFINE_LIST_END,
};

static const struct ext_duplicate_settings ext_duplicate_default_settings = {
	.default_period = (12*60*60),
	.max_period = (2*24*60*60),
};

const struct setting_parser_info ext_duplicate_setting_parser_info = {
	.name = "sieve_duplicate",

	.defines = ext_duplicate_setting_defines,
	.defaults = &ext_duplicate_default_settings,

	.struct_size = sizeof(struct ext_duplicate_settings),

	.pool_offset1 = 1 + offsetof(struct ext_duplicate_settings, pool),
};
