/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "buffer.h"
#include "settings-parser.h"
#include "service-settings.h"
#include "mail-storage-settings.h"

#include "pigeonhole-config.h"

#include "managesieve-settings.h"

#include <stddef.h>
#include <unistd.h>

static bool
managesieve_settings_verify(void *_set, pool_t pool, const char **error_r);

struct service_settings managesieve_settings_service_settings = {
	.name = "managesieve",
	.protocol = "sieve",
	.type = "",
	.executable = "managesieve",
	.user = "",
	.group = "",
	.privileged_group = "",
	.extra_groups = ARRAY_INIT,
	.chroot = "",

	.drop_priv_before_exec = FALSE,

	.client_limit = 1,
	.restart_request_count = 1,

	.unix_listeners = ARRAY_INIT,
	.fifo_listeners = ARRAY_INIT,
	.inet_listeners = ARRAY_INIT
};

const struct setting_keyvalue managesieve_settings_service_settings_defaults[] = {
	{ "unix_listener", "login\\ssieve srv.managesieve\\s%{pid}" },

	{ "unix_listener/login\\ssieve/path", "login/sieve" },
	{ "unix_listener/login\\ssieve/mode", "0666" },

	{ "unix_listener/srv.managesieve\\s%{pid}/path", "srv.managesieve/%{pid}" },
	{ "unix_listener/srv.managesieve\\s%{pid}/type", "admin" },
	{ "unix_listener/srv.managesieve\\s%{pid}/mode", "0600" },

	{ NULL, NULL }
};

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type(#name, name, struct managesieve_settings)

static struct setting_define managesieve_setting_defines[] = {
	DEF(BOOL, verbose_proctitle),
	DEF(STR, rawlog_dir),

	DEF(SIZE, managesieve_max_line_length),
	DEF(STR, managesieve_implementation_string),
	DEF(STR, managesieve_client_workarounds),
	DEF(STR_NOVARS, managesieve_logout_format),
	DEF(UINT, managesieve_max_compile_errors),

	SETTING_DEFINE_LIST_END
};

static struct managesieve_settings managesieve_default_settings = {
	.verbose_proctitle = FALSE,
	.rawlog_dir = "",

	/* RFC-2683 recommends at least 8000 bytes. Some clients however don't
	   break large message sets to multiple commands, so we're pretty
	   liberal by default. */
	.managesieve_max_line_length = 65536,
	.managesieve_implementation_string = DOVECOT_NAME " " PIGEONHOLE_NAME,
	.managesieve_client_workarounds = "",
	.managesieve_logout_format = "bytes=%{input}/%{output}",
	.managesieve_max_compile_errors = 5
};

const struct setting_parser_info managesieve_setting_parser_info = {
	.name = "managesieve",

	.defines = managesieve_setting_defines,
	.defaults = &managesieve_default_settings,

	.struct_size = sizeof(struct managesieve_settings),
	.pool_offset1 = 1 + offsetof(struct managesieve_settings, pool),

	.check_func = managesieve_settings_verify,
};

static const struct setting_define plugin_setting_defines[] = {
	{ .type = SET_STRLIST, .key = "plugin",
	  .offset = offsetof(struct plugin_settings, plugin_envs) },

	SETTING_DEFINE_LIST_END
};

const struct setting_parser_info managesieve_plugin_setting_parser_info = {
	.name = "managesieve_plugin",

	.defines = plugin_setting_defines,

	.struct_size = sizeof(struct plugin_settings),
	.pool_offset1 = 1 + offsetof(struct plugin_settings, pool),
};

const struct setting_parser_info *managesieve_settings_set_infos[] = {
	&managesieve_setting_parser_info,
	&managesieve_plugin_setting_parser_info,
	NULL
};

/* <settings checks> */
struct managesieve_client_workaround_list {
	const char *name;
	enum managesieve_client_workarounds num;
};

static const struct managesieve_client_workaround_list
managesieve_client_workaround_list[] = {
	{ NULL, 0 }
};

static int
managesieve_settings_parse_workarounds(struct managesieve_settings *set,
				       const char **error_r)
{
	enum managesieve_client_workarounds client_workarounds = 0;
	const struct managesieve_client_workaround_list *list;
	const char *const *str;

	str = t_strsplit_spaces(set->managesieve_client_workarounds, " ,");
	for (; *str != NULL; str++) {
		list = managesieve_client_workaround_list;
		for (; list->name != NULL; list++) {
			if (strcasecmp(*str, list->name) == 0) {
				client_workarounds |= list->num;
				break;
			}
		}
		if (list->name == NULL) {
			*error_r = t_strdup_printf(
				"managesieve_client_workarounds: "
				"Unknown workaround: %s", *str);
			return -1;
		}
	}
	set->parsed_workarounds = client_workarounds;
	return 0;
}


static bool
managesieve_settings_verify(void *_set, pool_t pool ATTR_UNUSED,
			    const char **error_r)
{
	struct managesieve_settings *set = _set;

	if (managesieve_settings_parse_workarounds(set, error_r) < 0)
		return FALSE;
	return TRUE;
}

/* </settings checks> */

const char *managesieve_settings_version = DOVECOT_ABI_VERSION;
