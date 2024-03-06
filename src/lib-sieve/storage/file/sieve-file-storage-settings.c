/* Copyright (c) 2024 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str-sanitize.h"
#include "array.h"
#include "settings.h"
#include "settings-parser.h"

#include "sieve-script.h"

#include "sieve-file-storage-settings.h"

#undef DEF
#define DEF(type, name) SETTING_DEFINE_STRUCT_##type( \
	"sieve_"#name, name, \
	struct sieve_file_storage_settings)

static const struct setting_define sieve_file_storage_setting_defines[] = {
	DEF(STR, script_path),
	DEF(STR, script_active_path),

	SETTING_DEFINE_LIST_END,
};

static const struct sieve_file_storage_settings sieve_file_storage_default_settings = {
	.script_path = "",
	.script_active_path = "",
};

const struct setting_parser_info sieve_file_storage_setting_parser_info = {
	.name = "sieve_file_storage",

	.defines = sieve_file_storage_setting_defines,
	.defaults = &sieve_file_storage_default_settings,

	.struct_size = sizeof(struct sieve_file_storage_settings),

	.pool_offset1 = 1 + offsetof(struct sieve_file_storage_settings, pool),
};
