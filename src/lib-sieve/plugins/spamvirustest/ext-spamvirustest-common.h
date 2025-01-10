#ifndef EXT_SPAMVIRUSTEST_COMMON_H
#define EXT_SPAMVIRUSTEST_COMMON_H

#include "sieve-common.h"

/*
 * Extensions
 */

extern const struct sieve_extension_def spamtest_extension;
extern const struct sieve_extension_def spamtestplus_extension;
extern const struct sieve_extension_def virustest_extension;

int ext_spamvirustest_load(const struct sieve_extension *ext, void **context_r);
void ext_spamvirustest_unload(const struct sieve_extension *ext);

/*
 * Tests
 */

extern const struct sieve_command_def spamtest_test;
extern const struct sieve_command_def virustest_test;

int ext_spamvirustest_get_value(const struct sieve_runtime_env *renv,
				const struct sieve_extension *ext,
				bool percent, const char **value_r);

/*
 * Operations
 */

extern const struct sieve_operation_def spamtest_operation;
extern const struct sieve_operation_def virustest_operation;

#endif
