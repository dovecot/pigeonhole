/* Copyright (c) 2002-2012 Sieve duplicate Plugin authors, see the included
 * COPYING file.
 */

#ifndef __EXT_DUPLICATE_COMMON_H
#define __EXT_DUPLICATE_COMMON_H

#include "sieve-common.h"

/*
 * Extension
 */

bool ext_duplicate_load
	(const struct sieve_extension *ext, void **context);
void ext_duplicate_unload
	(const struct sieve_extension *ext);

extern const struct sieve_extension_def duplicate_extension;

/* 
 * Tests
 */

extern const struct sieve_command_def tst_duplicate;

/*
 * Operations
 */

extern const struct sieve_operation_def tst_duplicate_operation;

/*
 * Duplicate checking
 */

bool ext_duplicate_check
	(const struct sieve_runtime_env *renv, string_t *name);

#endif /* EXT_DUPLICATE_COMMON_H */
