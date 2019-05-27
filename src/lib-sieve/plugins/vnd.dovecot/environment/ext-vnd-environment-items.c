/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"

#include "sieve-settings.h"
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
envit_default_mailbox_get_value(const struct sieve_runtime_env *renv,
				const char *name ATTR_UNUSED)
{
	const struct sieve_execute_env *eenv = renv->exec_env;

	i_assert(eenv->scriptenv->default_mailbox != NULL);
	return eenv->scriptenv->default_mailbox;
}

const struct sieve_environment_item default_mailbox_env_item = {
	.name = "vnd.dovecot.default-mailbox",
	.get_value = envit_default_mailbox_get_value
};

/* username */

static const char *
envit_username_get_value(const struct sieve_runtime_env *renv,
			 const char *name ATTR_UNUSED)
{
	const struct sieve_execute_env *eenv = renv->exec_env;

	return eenv->svinst->username;
}

const struct sieve_environment_item username_env_item = {
	.name = "vnd.dovecot.username",
	.get_value = envit_username_get_value
};

/* config.* */

static const char *
envit_config_get_value(const struct sieve_runtime_env *renv, const char *name)
{
	const struct sieve_execute_env *eenv = renv->exec_env;

	if (*name == '\0')
		return NULL;

	return sieve_setting_get(eenv->svinst,
				 t_strconcat("sieve_env_", name, NULL));
}

const struct sieve_environment_item config_env_item = {
	.name = "vnd.dovecot.config",
	.prefix = TRUE,
	.get_value = envit_config_get_value
};

/*
 * Register
 */

void ext_vnd_environment_items_register(const struct sieve_extension *ext,
					const struct sieve_runtime_env *renv)
{
	struct ext_vnd_environment_context *ectx =
		(struct ext_vnd_environment_context *)ext->context;

	sieve_environment_item_register(ectx->env_ext, renv->interp,
					&default_mailbox_env_item);
	sieve_environment_item_register(ectx->env_ext, renv->interp,
					&username_env_item);
	sieve_environment_item_register(ectx->env_ext, renv->interp,
					&config_env_item);
}
