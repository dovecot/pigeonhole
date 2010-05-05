/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "buffer.h"
#include "env-util.h"
#include "fd-close-on-exec.h"
#include "execv-const.h"
#include "settings-parser.h"
#include "service-settings.h"
#include "login-settings.h"
#include "managesieve-login-settings.h"

#include <stddef.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sysexits.h>

/* <settings checks> */
#ifdef _CONFIG_PLUGIN
static bool managesieve_login_settings_verify
	(void *_set, pool_t pool, const char **error_r);
#endif

static struct inet_listener_settings managesieve_login_inet_listeners_array[] = {
    { "managesieve", "", 4190, FALSE },
};
static struct inet_listener_settings *managesieve_login_inet_listeners[] = {
	&managesieve_login_inet_listeners_array[0]
};
static buffer_t managesieve_login_inet_listeners_buf = {
	managesieve_login_inet_listeners, sizeof(managesieve_login_inet_listeners), { 0, }
};
/* </settings checks> */

struct service_settings managesieve_login_settings_service_settings = {
	.name = "managesieve-login",
	.protocol = "managesieve",
	.type = "login",
	.executable = "managesieve-login",
	.user = "$default_login_user",
	.group = "",
	.privileged_group = "",
	.extra_groups = "",
	.chroot = "login",

	.drop_priv_before_exec = FALSE,

	.process_min_avail = 0,
	.process_limit = 0,
	.client_limit = 0,
	.service_count = 1,
	.vsz_limit = 64,

	.unix_listeners = ARRAY_INIT,
	.fifo_listeners = ARRAY_INIT,
	.inet_listeners = { { &managesieve_login_inet_listeners_buf,
		sizeof(managesieve_login_inet_listeners[0]) } }
};

#undef DEF
#define DEF(type, name) \
	{ type, #name, offsetof(struct managesieve_login_settings, name), NULL }

static const struct setting_define managesieve_login_setting_defines[] = {
	DEF(SET_STR, managesieve_implementation_string),
	DEF(SET_STR, managesieve_sieve_capability),
	DEF(SET_STR, managesieve_notify_capability),

	SETTING_DEFINE_LIST_END
};

static const struct managesieve_login_settings managesieve_login_default_settings = {
	.managesieve_implementation_string = PACKAGE_NAME,
	.managesieve_sieve_capability = "",
	.managesieve_notify_capability = ""
};

static const struct setting_parser_info *managesieve_login_setting_dependencies[] = {
	&login_setting_parser_info,
	NULL
};

static const struct setting_parser_info managesieve_login_setting_parser_info = {
	.module_name = "managesieve-login",
	.defines = managesieve_login_setting_defines,
	.defaults = &managesieve_login_default_settings,
	
	.type_offset = (size_t)-1,
	.struct_size = sizeof(struct managesieve_login_settings),

	.parent_offset = (size_t)-1,
	.parent = NULL,

	/* Only compiled in the doveconf plugin */
#ifdef _CONFIG_PLUGIN
	.check_func = managesieve_login_settings_verify,
#endif

	.dependencies = managesieve_login_setting_dependencies
};

const struct setting_parser_info *managesieve_login_settings_set_roots[] = {
	&login_setting_parser_info,
	&managesieve_login_setting_parser_info,
	NULL
};

/* 
 * Dynamic ManageSieve capability determination
 *   Only compiled in the doveconf plugin 
 */

#ifdef _CONFIG_PLUGIN

typedef enum { CAP_SIEVE, CAP_NOTIFY } capability_type_t;

static char *capability_sieve = NULL;
static char *capability_notify = NULL;

void managesieve_login_settings_deinit(void)
{
	if ( capability_sieve != NULL )
		i_free(capability_sieve);

	if ( capability_notify != NULL )
		i_free(capability_notify);
}

static void capability_store(capability_type_t cap_type, const char *value)
{
	switch ( cap_type ) {
	case CAP_SIEVE:
		capability_sieve = i_strdup(value);
		break;
	case CAP_NOTIFY:
		capability_notify = i_strdup(value);
		break;
	}
}

static void capability_parse(const char *cap_string)
{
	capability_type_t cap_type = CAP_SIEVE;
	const char *p = cap_string;
	string_t *part = t_str_new(256);

	if ( cap_string == NULL || *cap_string == '\0' ) {
		i_warning("managesieve-login: capability string is empty.");
		return;
	}
	
	while ( *p != '\0' ) {
		if ( *p == '\\' ) {
			p++;
			if ( *p != '\0' ) {
				str_append_c(part, *p);
				p++;
			} else break;
		} else if ( *p == ':' ) {
			if ( strcasecmp(str_c(part), "SIEVE") == 0 )
				cap_type = CAP_SIEVE;
			else if ( strcasecmp(str_c(part), "NOTIFY") == 0 )
				cap_type = CAP_NOTIFY;
			else
				i_warning("managesieve-login: unknown capability '%s' listed in "
					"capability string (ignored).", str_c(part));
			str_truncate(part, 0); 
		} else if ( *p == ',' ) {
			capability_store(cap_type, str_c(part));
			str_truncate(part, 0);
		} else {
			/* Append character, but omit leading spaces */
			if ( str_len(part) > 0 || *p != ' ' )
				str_append_c(part, *p);
		}
		p++;
	}
	
	if ( str_len(part) > 0 ) {
		capability_store(cap_type, str_c(part));
	}
}

static bool capability_dump(void)
{
	char buf[4096];
	int fd[2], status;
	ssize_t ret;
	unsigned int pos;
	pid_t pid;

	if ( pipe(fd) < 0 ) {
		i_error("managesieve-login: dump-capability pipe() failed: %m");
		return FALSE;
	}
	fd_close_on_exec(fd[0], TRUE);
	fd_close_on_exec(fd[1], TRUE);

	if ( (pid = fork()) == (pid_t)-1 ) {
		(void)close(fd[0]); (void)close(fd[1]);
		i_error("managesieve-login: dump-capability fork() failed: %m");
		return FALSE;
	}

	if ( pid == 0 ) {
		const char *argv[2];

		/* Child */
		(void)close(fd[0]);		
	
		if (dup2(fd[1], STDOUT_FILENO) < 0)
			i_fatal("managesieve-login: dump-capability dup2() failed: %m");

		env_put("DUMP_CAPABILITY=1");

		argv[0] = PKG_LIBEXECDIR"/managesieve"; /* BAD */
		argv[1] = NULL;
		execv_const(argv[0], argv);

		i_fatal("managesieve-login: dump-capability execv(%s) failed: %m", argv[0]);
	}

	(void)close(fd[1]);

	alarm(5);
	if (wait(&status) == -1) {
		i_error("managesieve-login: dump-capability failed: process %d got stuck", 
			(int)pid);
		return FALSE;
	}
	alarm(0);

	if (status != 0) {
		(void)close(fd[0]);
		if (WIFSIGNALED(status)) {
			i_error("managesieve-login: dump-capability process "
				"killed with signal %d", WTERMSIG(status));
		} else {
			i_error("managesieve-login: dump-capability process returned %d",
				WIFEXITED(status) ? WEXITSTATUS(status) : status);
		}
		return FALSE;
	}

	pos = 0;
	while ((ret = read(fd[0], buf + pos, sizeof(buf) - pos)) > 0)
		pos += ret;

	if (ret < 0) {
		i_error("managesieve-login: read(dump-capability process) failed: %m");
		(void)close(fd[0]);
		return FALSE;
	}
	(void)close(fd[0]);

	if (pos == 0 || buf[pos-1] != '\n') {
		i_error("managesieve-login: dump-capability: Couldn't read capability "
			"(got %u bytes)", pos);
		return FALSE;
	}
	buf[pos-1] = '\0';

	capability_parse(buf);

	return TRUE;
}

/* <settings checks> */
static bool managesieve_login_settings_verify
(void *_set, pool_t pool ATTR_UNUSED, const char **error_r ATTR_UNUSED)
{
	struct managesieve_login_settings *set = _set;

	if ( capability_sieve == NULL ) {
		if ( !capability_dump() ) {
			capability_sieve = "";
		}
	}

	if ( *set->managesieve_sieve_capability == '\0' && capability_sieve != NULL )
		set->managesieve_sieve_capability = capability_sieve;

	if ( *set->managesieve_notify_capability == '\0' && capability_notify != NULL )
		set->managesieve_notify_capability = capability_notify;
	
	return TRUE;
}
/* </settings checks> */

#endif /* _CONFIG_PLUGIN */
