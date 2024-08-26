/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "hash.h"

#include "sieve-common.h"
#include "sieve-extensions.h"
#include "sieve-interpreter.h"

#include "ext-environment-common.h"

struct ext_environment_interpreter_context;

/*
 * Core environment items
 */

static const struct sieve_environment_item_def *core_env_items[] = {
	&domain_env_item,
	&host_env_item,
	&location_env_item,
	&phase_env_item,
	&name_env_item,
	&version_env_item,
};

static unsigned int core_env_items_count = N_ELEMENTS(core_env_items);

static void
sieve_environment_item_insert(
	struct ext_environment_interpreter_context *ctx,
	struct sieve_interpreter *interp, const struct sieve_extension *ext,
	const struct sieve_environment_item_def *item_def);

/*
 * Validator context
 */

struct ext_environment_interpreter_context {
	HASH_TABLE(const char *,
		   const struct sieve_environment_item *) name_items;
	ARRAY(const struct sieve_environment_item *) prefix_items;

	bool active:1;
};

static void
ext_environment_interpreter_extension_free(const struct sieve_extension *ext,
					   struct sieve_interpreter *interp,
					   void *context);

struct sieve_interpreter_extension environment_interpreter_extension = {
	.ext_def = &environment_extension,
	.free = ext_environment_interpreter_extension_free,
};

static struct ext_environment_interpreter_context *
ext_environment_interpreter_context_create(
	const struct sieve_extension *this_ext,
	struct sieve_interpreter *interp)
{
	pool_t pool = sieve_interpreter_pool(interp);
	struct ext_environment_interpreter_context *ctx;

	ctx = p_new(pool, struct ext_environment_interpreter_context, 1);

	hash_table_create(&ctx->name_items, default_pool, 0, str_hash, strcmp);
	i_array_init(&ctx->prefix_items, 16);

	sieve_interpreter_extension_register(interp, this_ext,
					     &environment_interpreter_extension,
					     ctx);
	return ctx;
}

static void
ext_environment_interpreter_extension_free(
	const struct sieve_extension *ext ATTR_UNUSED,
	struct sieve_interpreter *interp ATTR_UNUSED, void *context)
{
	struct ext_environment_interpreter_context *ctx =
		(struct ext_environment_interpreter_context *)context;

	hash_table_destroy(&ctx->name_items);
	array_free(&ctx->prefix_items);
}

static struct ext_environment_interpreter_context *
ext_environment_interpreter_context_get(const struct sieve_extension *this_ext,
					struct sieve_interpreter *interp)
{
	struct ext_environment_interpreter_context *ctx =
		(struct ext_environment_interpreter_context *)
		sieve_interpreter_extension_get_context(interp, this_ext);

	if (ctx == NULL) {
		ctx = ext_environment_interpreter_context_create(
			this_ext, interp);
	}
	return ctx;
}

void ext_environment_interpreter_init(const struct sieve_extension *this_ext,
				      struct sieve_interpreter *interp)
{
	struct ext_environment_interpreter_context *ctx;
	unsigned int i;

	/* Create our context */
	ctx = ext_environment_interpreter_context_get(this_ext, interp);

	for (i = 0; i < core_env_items_count; i++) {
		sieve_environment_item_insert(ctx, interp, this_ext,
					      core_env_items[i]);
	}

	ctx->active = TRUE;
}

bool sieve_ext_environment_is_active(const struct sieve_extension *env_ext,
				     struct sieve_interpreter *interp)
{
	struct ext_environment_interpreter_context *ctx =
		ext_environment_interpreter_context_get(env_ext, interp);

	return (ctx != NULL && ctx->active);
}

/*
 * Registration
 */

static void
sieve_environment_item_insert(
	struct ext_environment_interpreter_context *ctx,
	struct sieve_interpreter *interp, const struct sieve_extension *ext,
	const struct sieve_environment_item_def *item_def)
{
	pool_t pool = sieve_interpreter_pool(interp);
	struct sieve_environment_item *item_mod;

	item_mod = p_new(pool, struct sieve_environment_item, 1);
	item_mod->def = item_def;
	item_mod->ext = ext;

	const struct sieve_environment_item *item = item_mod;
	if (!item_def->prefix)
		hash_table_insert(ctx->name_items, item_def->name, item);
	else
		array_append(&ctx->prefix_items, &item, 1);
}

void sieve_environment_item_register(
	const struct sieve_extension *env_ext, struct sieve_interpreter *interp,
	const struct sieve_extension *ext,
	const struct sieve_environment_item_def *item_def)
{
	struct ext_environment_interpreter_context *ctx;

	i_assert(sieve_extension_is(env_ext, environment_extension));
	ctx = ext_environment_interpreter_context_get(env_ext, interp);

	sieve_environment_item_insert(ctx, interp, ext, item_def);
}

/*
 * Retrieval
 */

static const struct sieve_environment_item *
ext_environment_item_lookup(struct ext_environment_interpreter_context *ctx,
			    const char **_name)
{
	const struct sieve_environment_item *item;
	const char *suffix, *name = *_name;

	item = hash_table_lookup(ctx->name_items, name);
	if (item != NULL)
		return item;

	array_foreach_elem(&ctx->prefix_items, item) {
		i_assert(item->def->prefix);

		if (str_begins(name, item->def->name, &suffix)) {
			if (*suffix == '.')
				++suffix;

			*_name = suffix;
			return item;
		}
	}
	return NULL;
}

const char *
ext_environment_item_get_value(const struct sieve_extension *env_ext,
			       const struct sieve_runtime_env *renv,
			       const char *name)
{
	struct ext_environment_interpreter_context *ctx;
	const struct sieve_environment_item *item;

	i_assert(sieve_extension_is(env_ext, environment_extension));
	ctx = ext_environment_interpreter_context_get(env_ext, renv->interp);

	item = ext_environment_item_lookup(ctx, &name);
	if (item == NULL)
		return NULL;

	i_assert(item->def != NULL);
	if (item->def->value != NULL)
		return item->def->value;
	if (item->def->get_value != NULL)
		return item->def->get_value(renv, item, name);
	return NULL;
}

/*
 * Default environment items
 */

/* "domain":

     The primary DNS domain associated with the Sieve execution context, usually
     but not always a proper suffix of the host name.
 */

static const char *
envit_domain_get_value(const struct sieve_runtime_env *renv,
		       const struct sieve_environment_item *item ATTR_UNUSED,
		       const char *name ATTR_UNUSED)
{
	const struct sieve_execute_env *eenv = renv->exec_env;

	return eenv->svinst->domainname;
}

const struct sieve_environment_item_def domain_env_item = {
	.name = "domain",
	.get_value = envit_domain_get_value,
};

/* "host":

     The fully-qualified domain name of the host where the Sieve script is
     executing.
 */

static const char *
envit_host_get_value(const struct sieve_runtime_env *renv,
		     const struct sieve_environment_item *item ATTR_UNUSED,
		     const char *name ATTR_UNUSED)
{
	const struct sieve_execute_env *eenv = renv->exec_env;

	return eenv->svinst->hostname;
}

const struct sieve_environment_item_def host_env_item = {
	.name = "host",
	.get_value = envit_host_get_value,
};

/* "location":

     Sieve evaluation can be performed at various different points as messages
     are processed. This item provides additional information about the type of
     service that is evaluating the script.  Possible values are:
      "MTA" - the Sieve script is being evaluated by a Message Transfer Agent
      "MDA" - evaluation is being performed by a Mail Delivery Agent
      "MUA" - evaluation is being performed by a Mail User Agent (right...)
      "MS"  - evaluation is being performed by a Message Store
 */

static const char *
envit_location_get_value(const struct sieve_runtime_env *renv,
			 const struct sieve_environment_item *item ATTR_UNUSED,
			 const char *name ATTR_UNUSED)
{
	const struct sieve_execute_env *eenv = renv->exec_env;

	switch (eenv->svinst->env_location ) {
	case SIEVE_ENV_LOCATION_MDA:
		return "MDA";
	case SIEVE_ENV_LOCATION_MTA:
		return "MTA";
	case SIEVE_ENV_LOCATION_MS:
		return "MS";
	default:
		break;
	}
	return NULL;
}

const struct sieve_environment_item_def location_env_item = {
	.name = "location",
	.get_value = envit_location_get_value
};

/* "phase":

     The point relative to final delivery where the Sieve script is being
     evaluated.  Possible values are "pre", "during", and "post", referring
     respectively to processing before, during, and after final delivery has
     taken place.
 */

static const char *
envit_phase_get_value(const struct sieve_runtime_env *renv,
		      const struct sieve_environment_item *item ATTR_UNUSED,
		      const char *name ATTR_UNUSED)
{
	const struct sieve_execute_env *eenv = renv->exec_env;

	switch (eenv->svinst->delivery_phase) {
	case SIEVE_DELIVERY_PHASE_PRE:
		return "pre";
	case SIEVE_DELIVERY_PHASE_DURING:
		return "during";
	case SIEVE_DELIVERY_PHASE_POST:
		return "post";
	default:
		break;
	}
	return NULL;
}

const struct sieve_environment_item_def phase_env_item = {
	.name = "phase",
	.get_value = envit_phase_get_value
};

/* "name":

    The product name associated with the Sieve interpreter.
 */

const struct sieve_environment_item_def name_env_item = {
	.name = "name",
	.value = PIGEONHOLE_NAME" Sieve"
};

/* "version":

   The product version associated with the Sieve interpreter. The meaning of the
   product version string is product-specific and should always be considered
   in the context of the product name given by the "name" item.
 */

const struct sieve_environment_item_def version_env_item = {
	.name = "version",
	.value = PIGEONHOLE_VERSION,
};
