#ifndef EXT_ENVIRONMENT_COMMON_H
#define EXT_ENVIRONMENT_COMMON_H

#include "lib.h"

#include "sieve-common.h"

#include "sieve-ext-environment.h"

/*
 * Extension
 */

extern const struct sieve_extension_def environment_extension;

/*
 * Commands
 */

extern const struct sieve_command_def tst_environment;

/*
 * Operations
 */

extern const struct sieve_operation_def tst_environment_operation;

/*
 * Environment items
 */

extern const struct sieve_environment_item_def domain_env_item;
extern const struct sieve_environment_item_def host_env_item;
extern const struct sieve_environment_item_def location_env_item;
extern const struct sieve_environment_item_def phase_env_item;
extern const struct sieve_environment_item_def name_env_item;
extern const struct sieve_environment_item_def version_env_item;

/*
 * Initialization
 */

bool ext_environment_init(const struct sieve_extension *ext, void **context);
void ext_environment_deinit(const struct sieve_extension *ext);

/*
 * Validator context
 */

void ext_environment_interpreter_init(const struct sieve_extension *this_ext,
				      struct sieve_interpreter *interp);

#endif
