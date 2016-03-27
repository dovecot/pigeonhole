/* Copyright (c) 2016 Pigeonhole authors, see the included COPYING file
 */

#ifndef __EXT_IMAPSIEVE_COMMON_H
#define __EXT_IMAPSIEVE_COMMON_H

#include "sieve-extensions.h"

#include "imap-sieve.h"

/*
 * Extensions
 */

extern const struct sieve_extension_def imapsieve_extension;
extern const struct sieve_extension_def imapsieve_extension_dummy;

/*
 * Environment items
 */

void ext_imapsieve_environment_items_register
	(const struct sieve_extension *ext,
		const struct sieve_runtime_env *renv);

#endif /* __EXT_IMAPSIEVE_COMMON_H */
