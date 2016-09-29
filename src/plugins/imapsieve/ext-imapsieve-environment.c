/* Copyright (c) 2016 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "mail-storage.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-runtime.h"

#include "sieve-ext-environment.h"

#include "ext-imapsieve-common.h"

/*
 * Environment items
 */

/* imap.user */

static const char *envit_imap_user_get_value
(const struct sieve_runtime_env *renv,
	const char *name ATTR_UNUSED)
{
	return renv->svinst->username;
}

const struct sieve_environment_item imap_user_env_item = {
	.name = "imap.user",
	.get_value = envit_imap_user_get_value
};

/* imap.email */

static const char *envit_imap_email_get_value
(const struct sieve_runtime_env *renv,
	const char *name ATTR_UNUSED)
{
	return sieve_get_user_email(renv->svinst);
}

const struct sieve_environment_item imap_email_env_item = {
	.name = "imap.email",
	.get_value = envit_imap_email_get_value
};

/* imap.cause */

static const char *envit_imap_cause_get_value
(const struct sieve_runtime_env *renv,
	const char *name ATTR_UNUSED)
{
	const struct sieve_script_env *senv = renv->scriptenv;
	struct imap_sieve_context *isctx =
		(struct imap_sieve_context *)senv->script_context;

	return isctx->event.cause;
}

const struct sieve_environment_item imap_cause_env_item = {
	.name = "imap.cause",
	.get_value = envit_imap_cause_get_value
};

/* imap.mailbox */

static const char *envit_imap_mailbox_get_value
(const struct sieve_runtime_env *renv,
	const char *name ATTR_UNUSED)
{
	const struct sieve_script_env *senv = renv->scriptenv;
	struct imap_sieve_context *isctx =
		(struct imap_sieve_context *)senv->script_context;

	return mailbox_get_vname(isctx->event.mailbox);
}

const struct sieve_environment_item imap_mailbox_env_item = {
	.name = "imap.mailbox",
	.get_value = envit_imap_mailbox_get_value
};


/* imap.changedflags */

static const char *envit_imap_changedflags_get_value
(const struct sieve_runtime_env *renv,
	const char *name ATTR_UNUSED)
{
	const struct sieve_script_env *senv = renv->scriptenv;
	struct imap_sieve_context *isctx =
		(struct imap_sieve_context *)senv->script_context;

	return isctx->event.changed_flags;
}

const struct sieve_environment_item imap_changedflags_env_item = {
	.name = "imap.changedflags",
	.get_value = envit_imap_changedflags_get_value
};

/*
 * Register
 */

void ext_imapsieve_environment_items_register
(const struct sieve_extension *ext, const struct sieve_runtime_env *renv)
{
	const struct sieve_extension *env_ext =
		(const struct sieve_extension *) ext->context;

	sieve_environment_item_register
		(env_ext, renv->interp, &imap_user_env_item);
	sieve_environment_item_register
		(env_ext, renv->interp, &imap_email_env_item);
	sieve_environment_item_register
		(env_ext, renv->interp, &imap_cause_env_item);
	sieve_environment_item_register
		(env_ext, renv->interp, &imap_mailbox_env_item);
	sieve_environment_item_register
		(env_ext, renv->interp, &imap_changedflags_env_item);
}
