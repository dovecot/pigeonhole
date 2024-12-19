/* Copyright (c) 2024 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "settings.h"
#include "settings-parser.h"

#include "sieve-limits.h"
#include "sieve-settings.h"

#include <ctype.h>

static bool sieve_settings_check(void *_set, pool_t pool, const char **error_r);

#undef DEF
#define DEF(type, name) SETTING_DEFINE_STRUCT_##type( \
	"sieve_"#name, name, struct sieve_settings)

static const struct setting_define sieve_setting_defines[] = {
	DEF(BOOL, enabled),

	DEF(SIZE, max_script_size),
	DEF(UINT, max_actions),
	DEF(UINT, max_redirects),
	DEF(TIME, max_cpu_time),
	DEF(TIME, resource_usage_timeout),

	DEF(STR, redirect_envelope_from),
	DEF(UINT, redirect_duplicate_period),

	DEF(STR, user_email),
	DEF(STR, user_log),

	DEF(STR, trace_dir),
	DEF(ENUM, trace_level),
	DEF(BOOL, trace_debug),
	DEF(BOOL, trace_addresses),

	DEF(BOOLLIST, plugins),
	DEF(STR, plugin_dir),

	DEF(BOOLLIST, extensions),
	DEF(BOOLLIST, global_extensions),
	DEF(BOOLLIST, implicit_extensions),

	SETTING_DEFINE_LIST_END,
};

const struct sieve_settings sieve_default_settings = {
	.enabled = TRUE,

	.max_script_size = (1 << 20),
	.max_actions = 32,
	.max_redirects = 4,
	.max_cpu_time = 0, /* FIXME: svinst->env_location == SIEVE_ENV_LOCATION_MS */

	.resource_usage_timeout = (60 * 60),
	.redirect_envelope_from = "",
	.redirect_duplicate_period = DEFAULT_REDIRECT_DUPLICATE_PERIOD,

	.user_email = "",
	.user_log = "",

	.trace_dir = "",
	.trace_level = "none:actions:commands:tests:matching",
	.trace_debug = FALSE,
	.trace_addresses = FALSE,

	.plugins = ARRAY_INIT,
	.plugin_dir = MODULEDIR"/sieve",

	.extensions = ARRAY_INIT,
	.global_extensions = ARRAY_INIT,
	.implicit_extensions = ARRAY_INIT,
};

static const struct setting_keyvalue sieve_default_settings_keyvalue[] = {
	{ "sieve_extensions",
	  "fileinto reject envelope encoded-character vacation subaddress "
	  "comparator-i;ascii-numeric relational regex imap4flags copy include "
	  "body variables enotify environment mailbox date index ihave "
	  "duplicate mime foreverypart extracttext"
	},
	{ NULL, NULL }
};

const struct setting_parser_info sieve_setting_parser_info = {
	.name = "sieve",

	.defines = sieve_setting_defines,
	.defaults = &sieve_default_settings,
	.default_settings = sieve_default_settings_keyvalue,

	.struct_size = sizeof(struct sieve_settings),

	.check_func = sieve_settings_check,

	.pool_offset1 = 1 + offsetof(struct sieve_settings, pool),
};

/* <settings checks> */
static bool
sieve_settings_check(void *_set, pool_t pool, const char **error_r)
{
	struct sieve_settings *set = _set;
	struct smtp_address *address = NULL;
	const char *error;

	if (!sieve_address_source_parse(
		pool, set->redirect_envelope_from,
		&set->parsed.redirect_envelope_from)) {
		*error_r = t_strdup_printf("sieve_redirect_envelope_from: "
					   "Invalid address source '%s'",
					   set->redirect_envelope_from);
		return FALSE;
	}

	if (*set->user_email != '\0' &&
	    smtp_address_parse_path(pool, set->user_email,
				    SMTP_ADDRESS_PARSE_FLAG_BRACKETS_OPTIONAL,
				    &address, &error) < 0) {
		*error_r = t_strdup_printf(
			"sieve_user_email: Invalid SMTP address '%s': %s",
			set->user_email, error);
		return FALSE;
	}
	set->parsed.user_email = address;

#ifdef CONFIG_BINARY
	if (array_is_created(&set->plugins) &&
	    array_not_empty(&set->plugins) &&
	    faccessat(AT_FDCWD, set->plugin_dir, R_OK | X_OK, AT_EACCESS) < 0) {
		*error_r = t_strdup_printf(
			"sieve_plugin_dir: access(%s) failed: %m",
			set->plugin_dir);
		return FALSE;
	}
#endif
	return TRUE;
}
/* </settings checks> */
