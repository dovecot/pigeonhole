/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"

#include "sieve-settings.old.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-address-parts.h"

#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"

#include "ext-vnd-environment-common.h"

/*
 * Environment items
 */

/* default_mailbox */

static const char *
envit_default_mailbox_get_value(
	const struct sieve_runtime_env *renv,
	const struct sieve_environment_item *item ATTR_UNUSED,
	const char *name ATTR_UNUSED)
{
	const struct sieve_execute_env *eenv = renv->exec_env;

	i_assert(eenv->scriptenv->default_mailbox != NULL);
	return eenv->scriptenv->default_mailbox;
}

const struct sieve_environment_item_def default_mailbox_env_item = {
	.name = "vnd.dovecot.default-mailbox",
	.get_value = envit_default_mailbox_get_value,
};

/* username */

static const char *
envit_username_get_value(const struct sieve_runtime_env *renv,
			 const struct sieve_environment_item *item ATTR_UNUSED,
			 const char *name ATTR_UNUSED)
{
	const struct sieve_execute_env *eenv = renv->exec_env;

	return eenv->svinst->username;
}

const struct sieve_environment_item_def username_env_item = {
	.name = "vnd.dovecot.username",
	.get_value = envit_username_get_value,
};

/* config.* */

static const char *
envit_config_get_value(const struct sieve_runtime_env *renv ATTR_UNUSED,
		       const struct sieve_environment_item *item,
		       const char *name)
{
	const struct sieve_extension *this_ext = item->ext;
	struct ext_vnd_environment_context *extctx = this_ext->context;

	if (*name == '\0')
		return NULL;
	if (!array_is_created(&extctx->set->envs))
		return NULL;

	const char *const *envs;
	unsigned int envs_count, i;

	envs = array_get(&extctx->set->envs, &envs_count);
	i_assert(envs_count % 2 == 0);
	for (i = 0; i < envs_count; i += 2) {
		if (strcasecmp(name, envs[i]) == 0)
			return envs[i + 1];
	}
	return NULL;
}

const struct sieve_environment_item_def config_env_item = {
	.name = "vnd.dovecot.config",
	.prefix = TRUE,
	.get_value = envit_config_get_value,
};

/*
 * Register
 */

void ext_vnd_environment_items_register(const struct sieve_extension *ext,
					const struct sieve_runtime_env *renv)
{
	struct ext_vnd_environment_context *extctx = ext->context;

	sieve_environment_item_register(extctx->env_ext, renv->interp, ext,
					&default_mailbox_env_item);
	sieve_environment_item_register(extctx->env_ext, renv->interp, ext,
					&username_env_item);
	sieve_environment_item_register(extctx->env_ext, renv->interp, ext,
					&config_env_item);
}
