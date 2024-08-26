/* Copyright (c) 2024 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "settings.h"
#include "settings-parser.h"

#include "ext-vnd-environment-settings.h"

static const struct setting_define ext_vnd_environment_setting_defines[] = {
	{ .type = SET_STRLIST, .key = "sieve_environment",
	  .offset = offsetof(struct ext_vnd_environment_settings, envs) },

	SETTING_DEFINE_LIST_END,
};

static const struct ext_vnd_environment_settings
ext_vnd_environment_default_settings = {
	.envs = ARRAY_INIT,
};

const struct setting_parser_info ext_vnd_environment_setting_parser_info = {
	.name = "sieve_vnd_environment",

	.defines = ext_vnd_environment_setting_defines,
	.defaults = &ext_vnd_environment_default_settings,

	.struct_size = sizeof(struct ext_vnd_environment_settings),

	.pool_offset1 = 1 + offsetof(struct ext_vnd_environment_settings, pool),
};
