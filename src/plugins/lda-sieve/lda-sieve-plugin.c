/* Copyright (C) 2006 Timo Sirainen */

#include "lib.h"
#include "home-expand.h"
#include "deliver.h"
#include "sieve-compiler.h"

#include "lda-sieve-plugin.h"

#include <stdlib.h>
#include <sys/stat.h>

#define SIEVE_SCRIPT_PATH "~/.dovecot.sieve"

static deliver_mail_func_t *next_deliver_mail;
struct et_list *_et_list = NULL;

static const char *get_sieve_path(void)
{
	const char *script_path, *home;
	struct stat st;

	home = getenv("HOME");

	/* userdb may specify Sieve path */
	script_path = getenv("SIEVE");
	if (script_path != NULL) {
		if (*script_path == '\0') {
			/* disabled */
			return NULL;
		}

		if (*script_path != '/' && *script_path != '\0') {
			/* relative path. change to absolute. */
			script_path = t_strconcat(getenv("HOME"), "/",
						  script_path, NULL);
		}
	} else {
		if (home == NULL) {
			i_error("Per-user script path is unknown. See "
				"http://wiki.dovecot.org/LDA/Sieve#location");
			return NULL;
		}

		script_path = home_expand(SIEVE_SCRIPT_PATH);
	}

	if (stat(script_path, &st) < 0) {
		if (errno != ENOENT)
			i_error("stat(%s) failed: %m", script_path);

		/* use global script instead, if one exists */
		script_path = getenv("SIEVE_GLOBAL_PATH");
		if (script_path == NULL) {
			/* for backwards compatibility */
			script_path = getenv("GLOBAL_SCRIPT_PATH");
		}
	}

	return script_path;
}

static int
lda_sieve_deliver_mail(struct mail_namespace *namespaces,
		      struct mail_storage **storage_r,
		      struct mail *mail,
		      const char *destaddr, const char *mailbox)
{
	const char *script_path;

	script_path = get_sieve_path();
	if (script_path == NULL)
		return 0;

	if (getenv("DEBUG") != NULL)
		i_info("sieve: Using sieve path: %s", script_path);

	return sieve_run(namespaces, storage_r, mail, script_path,
			     destaddr, getenv("USER"), mailbox);
}

void lda_sieve_plugin_init(void)
{
	next_deliver_mail = deliver_mail;
	deliver_mail = lda_sieve_deliver_mail;
}

void lda_sieve_plugin_deinit(void)
{
	deliver_mail = next_deliver_mail;
}
