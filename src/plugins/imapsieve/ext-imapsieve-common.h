/* Copyright (c) 2016-2018 Pigeonhole authors, see the included COPYING file
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

extern const struct sieve_extension_def vnd_imapsieve_extension;
extern const struct sieve_extension_def vnd_imapsieve_extension_dummy;

/*
 * Environment items
 */

void ext_imapsieve_environment_items_register
	(const struct sieve_extension *ext,
		const struct sieve_runtime_env *renv);
void ext_imapsieve_environment_vendor_items_register
	(const struct sieve_extension *ext,
		const struct sieve_runtime_env *renv);

#endif /* __EXT_IMAPSIEVE_COMMON_H */
