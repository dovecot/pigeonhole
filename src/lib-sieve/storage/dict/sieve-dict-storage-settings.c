/* Copyright (c) 2025 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "settings-parser.h"

#include "sieve-dict-storage-settings.h"

static const struct setting_define sieve_dict_storage_setting_defines[] = {
	{ .type = SET_FILTER_NAME, .key = "sieve_script_dict" },

	SETTING_DEFINE_LIST_END,
};

const struct setting_parser_info sieve_dict_storage_setting_parser_info = {
	.name = "sieve_dict_storage",
	.defines = sieve_dict_storage_setting_defines,
};
