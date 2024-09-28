/* Copyright (c) 2024 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "settings.h"
#include "settings-parser.h"

#include "managesieve-url.h"
#include "imap-sieve-settings.h"

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("imapsieve_"#name, name, \
				     struct imap_sieve_settings)

static const struct setting_define imap_sieve_setting_defines[] = {
	DEF(STR, url),
	DEF(BOOL, expunge_discarded),

	SETTING_DEFINE_LIST_END,
};

static const struct imap_sieve_settings imap_sieve_default_settings = {
	.url = "",
	.expunge_discarded = FALSE,
};

static bool
imap_sieve_settings_check(void *_set ATTR_UNUSED, pool_t pool ATTR_UNUSED,
			  const char **error_r ATTR_UNUSED);

const struct setting_parser_info imap_sieve_setting_parser_info = {
	.name = "imapsieve",

	.defines = imap_sieve_setting_defines,
	.defaults = &imap_sieve_default_settings,

	.struct_size = sizeof(struct imap_sieve_settings),

	.check_func = imap_sieve_settings_check,

	.pool_offset1 = 1 + offsetof(struct imap_sieve_settings, pool),
};

/* <settings checks> */
static bool
imap_sieve_settings_check(void *_set, pool_t pool ATTR_UNUSED,
			  const char **error_r)
{
	struct imap_sieve_settings *set = _set;
	const char *error;

	if (*set->url != '\0' &&
	    managesieve_url_parse(set->url, 0, pool_datastack_create(),
				  NULL, &error) < 0) {
		*error_r = t_strdup_printf(
			"Invalid URL for imapsieve_url setting: %s",
			set->url);
		return FALSE;
	}

	return TRUE;
}
/* </settings checks> */
