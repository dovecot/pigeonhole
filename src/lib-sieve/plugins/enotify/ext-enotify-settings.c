/* Copyright (c) 2026 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "settings.h"
#include "settings-parser.h"

#include "ext-enotify-settings.h"

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("sieve_notify_"#name, name, \
				     struct ext_enotify_settings)

static const struct setting_define ext_enotify_setting_defines[] = {
	DEF(UINT_HIDDEN, max_notifications),

	SETTING_DEFINE_LIST_END,
};

static const struct ext_enotify_settings ext_enotify_default_settings = {
	.max_notifications = 10,
};

const struct setting_parser_info ext_enotify_setting_parser_info = {
	.name = "sieve_notify",

	.defines = ext_enotify_setting_defines,
	.defaults = &ext_enotify_default_settings,

	.struct_size = sizeof(struct ext_enotify_settings),

	.pool_offset1 = 1 + offsetof(struct ext_enotify_settings, pool),
};
