/* Copyright (c) 2002-2016 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SIEVE_EXT_ENVIRONMENT_H
#define __SIEVE_EXT_ENVIRONMENT_H

#include "sieve-common.h"

/*
 * Environment extension
 */

/* FIXME: this is not suitable for future plugin support */

extern const struct sieve_extension_def environment_extension;

static inline const struct sieve_extension *
sieve_ext_environment_get_extension
(struct sieve_instance *svinst)
{
	return sieve_extension_register
		(svinst, &environment_extension, FALSE);
}

static inline const struct sieve_extension *
sieve_ext_environment_require_extension
(struct sieve_instance *svinst)
{
	return sieve_extension_require
		(svinst, &environment_extension, TRUE);
}

bool sieve_ext_environment_is_active
	(const struct sieve_extension *env_ext,
		struct sieve_interpreter *interp);

/*
 * Environment item
 */

struct sieve_environment_item {
	const char *name;
	bool prefix;

	const char *value;
	const char *(*get_value)
		(const struct sieve_runtime_env *renv, const char *name);
};

void sieve_environment_item_register
	(const struct sieve_extension *env_ext, struct sieve_interpreter *interp,
		const struct sieve_environment_item *item);
const char *ext_environment_item_get_value
	(const struct sieve_extension *env_ext,
		const struct sieve_runtime_env *renv, const char *name);

#endif
