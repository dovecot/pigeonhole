/* Copyright (c) 2024 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "settings.h"
#include "settings-parser.h"

#include "ntfy-mailto-settings.h"

static bool
ntfy_mailto_settings_check(void *_set, pool_t pool, const char **error_r);

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("sieve_notify_mailto_"#name, name, \
				     struct ntfy_mailto_settings)

static const struct setting_define ntfy_mailto_setting_defines[] = {
	DEF(STR, envelope_from),

	SETTING_DEFINE_LIST_END,
};

static const struct ntfy_mailto_settings ntfy_mailto_default_settings = {
	.envelope_from = "",
};

const struct setting_parser_info ntfy_mailto_setting_parser_info = {
	.name = "sieve_notify_mailto",

	.defines = ntfy_mailto_setting_defines,
	.defaults = &ntfy_mailto_default_settings,

	.struct_size = sizeof(struct ntfy_mailto_settings),

	.check_func = ntfy_mailto_settings_check,

	.pool_offset1 = 1 + offsetof(struct ntfy_mailto_settings, pool),
};

/* <settings checks> */
static bool
ntfy_mailto_settings_check(void *_set, pool_t pool, const char **error_r)
{
	struct ntfy_mailto_settings *set = _set;

	if (!sieve_address_source_parse(pool, set->envelope_from,
					&set->parsed.envelope_from)) {
		*error_r = t_strdup_printf("sieve_notify_mailto_envelope_from: "
					   "Invalid address source '%s'",
					   set->envelope_from);
		return FALSE;
	}

	return TRUE;
}
/* </settings checks> */
