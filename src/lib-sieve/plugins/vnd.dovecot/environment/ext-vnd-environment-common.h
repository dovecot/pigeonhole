/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#ifndef __EXT_VND_ENVIRONMENT_COMMON_H
#define __EXT_VND_ENVIRONMENT_COMMON_H

#include "sieve-ext-environment.h"

/*
 * Extension
 */

struct ext_vnd_environment_context {
	const struct sieve_extension *env_ext;
	const struct sieve_extension *var_ext;
};

extern const struct sieve_extension_def vnd_environment_extension;

/*
 * Operands
 */

extern const struct sieve_operand_def environment_namespace_operand;

/*
 * Environment items
 */

void ext_vnd_environment_items_register
(const struct sieve_extension *ext, const struct sieve_runtime_env *renv);

/*
 * Variables
 */

void ext_environment_variables_init
(const struct sieve_extension *this_ext, struct sieve_validator *valdtr);

#endif
