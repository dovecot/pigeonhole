/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "buffer.h"
#include "env-util.h"
#include "execv-const.h"
#include "master-service.h"
#include "settings-parser.h"
#include "config-parser-private.h"
#include "managesieve-login-settings-plugin.h"

#include <stddef.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sysexits.h>

typedef enum { CAP_SIEVE, CAP_NOTIFY } capability_type_t;

bool capability_dumped = FALSE;
static char *capability_sieve = NULL;
static char *capability_notify = NULL;

static void (*next_hook_config_parser_begin)(struct config_parser_context *ctx) = NULL;

static void managesieve_login_config_parser_begin(struct config_parser_context *ctx);

const char *managesieve_login_settings_version = DOVECOT_ABI_VERSION;

void managesieve_login_settings_init(struct module *module ATTR_UNUSED)
{
	next_hook_config_parser_begin = hook_config_parser_begin;
	hook_config_parser_begin = managesieve_login_config_parser_begin;
}

void managesieve_login_settings_deinit(void)
{
	hook_config_parser_begin = next_hook_config_parser_begin;

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
	int fd[2], status = 0;
	ssize_t ret;
	unsigned int pos;
	pid_t pid;

	if ( getenv("DUMP_CAPABILITY") != NULL )
		return TRUE;

	/* We want to dump capability only when doing the main config parsing
	   (config and doveconf processes) and managesieve process (started
	   from command line). We especially don't want to dump capability
	   every time when running doveadm. */
	const char *protocol = getenv("DOVECONF_PROTOCOL");
	if (protocol != NULL && strcmp(protocol, "sieve") != 0)
		return TRUE;
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
		const char *argv[5];

		/* Child */
		(void)close(fd[0]);

		if (dup2(fd[1], STDOUT_FILENO) < 0)
			i_fatal("managesieve-login: dump-capability dup2() failed: %m");

		env_put("DUMP_CAPABILITY", "1");

		argv[0] = PKG_LIBEXECDIR"/managesieve";
		argv[1] = "-k";
		argv[2] = "-c";
		argv[3] = master_service_get_config_path(master_service);
		argv[4] = NULL;
		execv_const(argv[0], argv);

		i_fatal("managesieve-login: dump-capability execv(%s) failed: %m", argv[0]);
	}

	(void)close(fd[1]);

	time_t start_time = time(NULL);
	alarm(60);
	pid_t wait_ret = wait(&status);
	alarm(0);

	if (wait_ret >= 0)
		; /* success */
	else if (errno != ECHILD) {
		i_error("managesieve-login: dump-capability failed: "
			"wait() failed: %m");
		return FALSE;
	} else {
		i_error("managesieve-login: dump-capability failed: "
			"process %d got stuck (waited %"PRIdTIME_T" seconds) - "
			"killing sig SIGABRT",
			(int)pid, (time(NULL) - start_time));
		if (kill(pid, SIGABRT) < 0)
			i_error("kill(%d) failed: %m", (int)pid);
		return FALSE;
	}

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

static void managesieve_login_config_set
(struct config_parser_context *ctx, const char *key, const char *value)
{
	config_parser_set_change_counter(ctx, CONFIG_PARSER_CHANGE_DEFAULTS);
	config_apply_line(ctx, key, value, NULL);
	config_parser_set_change_counter(ctx, CONFIG_PARSER_CHANGE_EXPLICIT);
}

static void managesieve_login_config_parser_begin(struct config_parser_context *ctx)
{	
	if ( !capability_dumped ) {
		(void)capability_dump();
		capability_dumped = TRUE;
	}

	if ( capability_sieve != NULL )
		managesieve_login_config_set(ctx, "managesieve_sieve_capability", capability_sieve);

	if ( capability_notify != NULL )
		managesieve_login_config_set(ctx, "managesieve_notify_capability", capability_notify);
}
