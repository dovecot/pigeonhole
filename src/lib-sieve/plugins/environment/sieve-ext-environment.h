#ifndef SIEVE_EXT_ENVIRONMENT_H
#define SIEVE_EXT_ENVIRONMENT_H

#include "sieve-common.h"

/*
 * Environment extension
 */

/* FIXME: this is not suitable for future plugin support */

extern const struct sieve_extension_def environment_extension;

static inline int
sieve_ext_environment_get_extension(struct sieve_instance *svinst,
				     const struct sieve_extension **ext_r)
{
	return sieve_extension_register(svinst, &environment_extension, FALSE,
					ext_r);
}

static inline int
sieve_ext_environment_require_extension(struct sieve_instance *svinst,
					const struct sieve_extension **ext_r)
{
	return sieve_extension_require(svinst, &environment_extension, TRUE,
				       ext_r);
}

bool sieve_ext_environment_is_active(const struct sieve_extension *env_ext,
				     struct sieve_interpreter *interp);

/*
 * Environment item
 */

struct sieve_environment_item;

struct sieve_environment_item_def {
	const char *name;
	bool prefix;

	const char *value;
	const char *(*get_value)(const struct sieve_runtime_env *renv,
				 const struct sieve_environment_item *item,
				 const char *name);
};

struct sieve_environment_item {
	const struct sieve_environment_item_def *def;
	const struct sieve_extension *ext;
};

void sieve_environment_item_register(
	const struct sieve_extension *env_ext, struct sieve_interpreter *interp,
	const struct sieve_extension *ext,
	const struct sieve_environment_item_def *item_def);
const char *
ext_environment_item_get_value(const struct sieve_extension *env_ext,
			       const struct sieve_runtime_env *renv,
			       const char *name);

#endif
