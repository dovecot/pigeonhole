#ifndef EXT_DUPLICATE_COMMON_H
#define EXT_DUPLICATE_COMMON_H

#include "sieve-common.h"

/*
 * Extension
 */

struct ext_duplicate_context {
	const struct ext_duplicate_settings *set;
};

int ext_duplicate_load(const struct sieve_extension *ext, void **context_r);
void ext_duplicate_unload(const struct sieve_extension *ext);

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

int ext_duplicate_check(const struct sieve_runtime_env *renv, string_t *handle,
			const char *value, size_t value_len,
			sieve_number_t period, bool last, bool *duplicate_r);

#endif
