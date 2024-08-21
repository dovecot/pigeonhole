/* Copyright (c) 2024 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "settings.h"
#include "settings-parser.h"

#include "ext-subaddress-settings.h"

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type(#name, name, \
				     struct ext_subaddress_settings)

static const struct setting_define ext_subaddress_setting_defines[] = {
	DEF(STR, recipient_delimiter),

	SETTING_DEFINE_LIST_END,
};

static const struct ext_subaddress_settings ext_subaddress_default_settings = {
	.recipient_delimiter = "+",
};

const struct setting_parser_info ext_subaddress_setting_parser_info = {
	.name = "sieve_subaddress",

	.defines = ext_subaddress_setting_defines,
	.defaults = &ext_subaddress_default_settings,

	.struct_size = sizeof(struct ext_subaddress_settings),

	.pool_offset1 = 1 + offsetof(struct ext_subaddress_settings, pool),
};
