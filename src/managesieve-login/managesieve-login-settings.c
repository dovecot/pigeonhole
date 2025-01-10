/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "buffer.h"
#include "env-util.h"
#include "execv-const.h"
#include "settings-parser.h"
#include "service-settings.h"
#include "login-settings.h"

#include "pigeonhole-config.h"
#include "managesieve-protocol.h"

#include "managesieve-login-settings.h"

#include <stddef.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sysexits.h>

struct service_settings managesieve_login_settings_service_settings = {
	.name = "managesieve-login",
	.protocol = "sieve",
	.type = "login",
	.executable = "managesieve-login",
	.user = "$default_login_user",
	.group = "",
	.privileged_group = "",
	.extra_groups = "",
	.chroot = "login",

	.drop_priv_before_exec = FALSE,

	.service_count = 1,

	.unix_listeners = ARRAY_INIT,
	.fifo_listeners = ARRAY_INIT,
	.inet_listeners = ARRAY_INIT,
};

const struct setting_keyvalue managesieve_login_settings_service_settings_defaults[] = {
	{ "unix_listener", "srv.managesieve-login\\s%{pid}" },

	{ "unix_listener/srv.managesieve-login\\s%{pid}/path", "srv.managesieve-login/%{pid}" },
	{ "unix_listener/srv.managesieve-login\\s%{pid}/type", "admin" },
	{ "unix_listener/srv.managesieve-login\\s%{pid}/mode", "0600" },

	{ "inet_listener", "sieve" },

	{ "inet_listener/sieve/port", "4190" },

	{ NULL, NULL }
};
#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type(#name, name, struct managesieve_login_settings)

static const struct setting_define managesieve_login_setting_defines[] = {
	DEF(STR, managesieve_implementation_string),
	DEF(STR, managesieve_sieve_capability),
	DEF(STR, managesieve_notify_capability),

	SETTING_DEFINE_LIST_END
};

static const struct managesieve_login_settings managesieve_login_default_settings = {
	.managesieve_implementation_string = DOVECOT_NAME " " PIGEONHOLE_NAME,
	.managesieve_sieve_capability = "",
	.managesieve_notify_capability = "",
};

const struct setting_parser_info managesieve_login_setting_parser_info = {
	.name = "managesieve_login",

	.defines = managesieve_login_setting_defines,
	.defaults = &managesieve_login_default_settings,

	.struct_size = sizeof(struct managesieve_login_settings),
	.pool_offset1 = 1 + offsetof(struct managesieve_login_settings, pool),
};

const struct setting_parser_info *managesieve_login_settings_set_infos[] = {
	&managesieve_login_setting_parser_info,
	NULL
};
