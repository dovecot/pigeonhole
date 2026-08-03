/* Copyright (c) Pigeonhole authors, see top-level COPYING file */

#include "lib.h"
#include "settings.h"
#include "settings-parser.h"

#include "imap-filter-sieve-settings.h"

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("imap_filter_sieve_"#name, name, \
				     struct imap_filter_sieve_settings)

static const struct setting_define imap_filter_sieve_setting_defines[] = {
	DEF(UINT, max_redirects),

	SETTING_DEFINE_LIST_END,
};

static const struct imap_filter_sieve_settings imap_filter_sieve_default_settings = {
	.max_redirects = 1024,
};

const struct setting_parser_info imap_filter_sieve_setting_parser_info = {
	.name = "imap_filter_sieve",

	.defines = imap_filter_sieve_setting_defines,
	.defaults = &imap_filter_sieve_default_settings,

	.struct_size = sizeof(struct imap_filter_sieve_settings),

	.pool_offset1 = 1 + offsetof(struct imap_filter_sieve_settings, pool),
};
