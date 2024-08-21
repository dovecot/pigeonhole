/* Copyright (c) 2024 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "settings.h"
#include "settings-parser.h"

#include "ext-include-limits.h"
#include "ext-include-settings.h"

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("sieve_include_"#name, name, \
				     struct ext_include_settings)

static const struct setting_define ext_include_setting_defines[] = {
	DEF(UINT, max_nesting_depth),
	DEF(UINT, max_includes),

	SETTING_DEFINE_LIST_END,
};

static const struct ext_include_settings ext_include_default_settings = {
	.max_nesting_depth = EXT_INCLUDE_DEFAULT_MAX_NESTING_DEPTH,
	.max_includes = EXT_INCLUDE_DEFAULT_MAX_INCLUDES,
};

const struct setting_parser_info ext_include_setting_parser_info = {
	.name = "sieve_include",

	.defines = ext_include_setting_defines,
	.defaults = &ext_include_default_settings,

	.struct_size = sizeof(struct ext_include_settings),

	.pool_offset1 = 1 + offsetof(struct ext_include_settings, pool),
};
