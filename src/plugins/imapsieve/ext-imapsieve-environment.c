/* Copyright (c) 2016-2018 Pigeonhole authors, see the included COPYING file
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

static const char *
envit_imap_user_get_value(
	const struct sieve_runtime_env *renv,
	const struct sieve_environment_item *item ATTR_UNUSED,
	const char *name ATTR_UNUSED)
{
	const struct sieve_execute_env *eenv = renv->exec_env;

	return eenv->svinst->username;
}

const struct sieve_environment_item_def imap_user_env_item = {
	.name = "imap.user",
	.get_value = envit_imap_user_get_value
};

/* imap.email */

static const char *
envit_imap_email_get_value(
	const struct sieve_runtime_env *renv,
	const struct sieve_environment_item *item ATTR_UNUSED,
	const char *name ATTR_UNUSED)
{
	const struct sieve_execute_env *eenv = renv->exec_env;
	const struct smtp_address *user_email =
		sieve_get_user_email(eenv->svinst);

	if (user_email == NULL)
		return NULL;
	return smtp_address_encode(user_email);
}

const struct sieve_environment_item_def imap_email_env_item = {
	.name = "imap.email",
	.get_value = envit_imap_email_get_value
};

/* imap.cause */

static const char *
envit_imap_cause_get_value(
	const struct sieve_runtime_env *renv,
	const struct sieve_environment_item *item ATTR_UNUSED,
	const char *name ATTR_UNUSED)
{
	const struct sieve_execute_env *eenv = renv->exec_env;
	const struct sieve_script_env *senv = eenv->scriptenv;
	struct imap_sieve_context *isctx =
		(struct imap_sieve_context *)senv->script_context;

	return isctx->event.cause;
}

const struct sieve_environment_item_def imap_cause_env_item = {
	.name = "imap.cause",
	.get_value = envit_imap_cause_get_value
};

/* imap.mailbox */

static const char *
envit_imap_mailbox_get_value(
	const struct sieve_runtime_env *renv,
	const struct sieve_environment_item *item ATTR_UNUSED,
	const char *name ATTR_UNUSED)
{
	const struct sieve_execute_env *eenv = renv->exec_env;
	const struct sieve_message_data *msgdata = eenv->msgdata;

	return mailbox_get_vname(msgdata->mail->box);
}

const struct sieve_environment_item_def imap_mailbox_env_item = {
	.name = "imap.mailbox",
	.get_value = envit_imap_mailbox_get_value
};


/* imap.changedflags */

static const char *
envit_imap_changedflags_get_value(
	const struct sieve_runtime_env *renv,
	const struct sieve_environment_item *item ATTR_UNUSED,
	const char *name ATTR_UNUSED)
{
	const struct sieve_execute_env *eenv = renv->exec_env;
	const struct sieve_script_env *senv = eenv->scriptenv;
	struct imap_sieve_context *isctx =
		(struct imap_sieve_context *)senv->script_context;

	return isctx->event.changed_flags;
}

const struct sieve_environment_item_def imap_changedflags_env_item = {
	.name = "imap.changedflags",
	.get_value = envit_imap_changedflags_get_value
};

/* vnd.dovecot.mailbox-from */

static const char *
envit_vnd_mailbox_from_get_value(
	const struct sieve_runtime_env *renv,
	const struct sieve_environment_item *item ATTR_UNUSED,
	const char *name ATTR_UNUSED)
{
	const struct sieve_execute_env *eenv = renv->exec_env;
	const struct sieve_script_env *senv = eenv->scriptenv;
	struct imap_sieve_context *isctx =
		(struct imap_sieve_context *)senv->script_context;

	return mailbox_get_vname(isctx->event.src_mailbox);
}

const struct sieve_environment_item_def vnd_mailbox_from_env_item = {
	.name = "vnd.dovecot.mailbox-from",
	.get_value = envit_vnd_mailbox_from_get_value
};

/* vnd.dovecot.mailbox-to */

static const char *
envit_vnd_mailbox_to_get_value(
	const struct sieve_runtime_env *renv,
	const struct sieve_environment_item *item ATTR_UNUSED,
	const char *name ATTR_UNUSED)
{
	const struct sieve_execute_env *eenv = renv->exec_env;
	const struct sieve_script_env *senv = eenv->scriptenv;
	struct imap_sieve_context *isctx =
		(struct imap_sieve_context *)senv->script_context;

	return mailbox_get_vname(isctx->event.dest_mailbox);
}

const struct sieve_environment_item_def vnd_mailbox_to_env_item= {
	.name = "vnd.dovecot.mailbox-to",
	.get_value = envit_vnd_mailbox_to_get_value
};

/*
 * Register
 */

void ext_imapsieve_environment_items_register(
	const struct sieve_extension *ext, const struct sieve_runtime_env *renv)
{
	struct ext_imapsieve_context *extctx = ext->context;
	const struct sieve_extension *env_ext = extctx->ext_environment;

	sieve_environment_item_register(env_ext, renv->interp, ext,
					&imap_user_env_item);
	sieve_environment_item_register(env_ext, renv->interp, ext,
					&imap_email_env_item);
	sieve_environment_item_register(env_ext, renv->interp, ext,
					&imap_cause_env_item);
	sieve_environment_item_register(env_ext, renv->interp, ext,
					&imap_mailbox_env_item);
	sieve_environment_item_register(env_ext, renv->interp, ext,
					&imap_changedflags_env_item);
}

void ext_imapsieve_environment_vendor_items_register(
	const struct sieve_extension *ext, const struct sieve_runtime_env *renv)
{
	struct ext_imapsieve_context *extctx = ext->context;
	const struct sieve_extension *env_ext = extctx->ext_environment;

	sieve_environment_item_register(env_ext, renv->interp, ext,
					&vnd_mailbox_from_env_item);
	sieve_environment_item_register(env_ext, renv->interp, ext,
					&vnd_mailbox_to_env_item);
}
