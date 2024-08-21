#ifndef EXT_VACATION_COMMON_H
#define EXT_VACATION_COMMON_H

#include "sieve-common.h"

#include "ext-vacation-settings.h"

/*
 * Commands
 */

extern const struct sieve_command_def vacation_command;

/*
 * Operations
 */

extern const struct sieve_operation_def vacation_operation;

/*
 * Context
 */

struct ext_vacation_context {
	const struct ext_vacation_settings *set;
};

/*
 * Extensions
 */

/* Vacation */

extern const struct sieve_extension_def vacation_extension;

int ext_vacation_load(const struct sieve_extension *ext, void **context);
void ext_vacation_unload(const struct sieve_extension *ext);

/* Vacation-seconds */

extern const struct sieve_extension_def vacation_seconds_extension;

bool ext_vacation_register_seconds_tag(
	struct sieve_validator *valdtr,
	const struct sieve_extension *vacation_ext);

#endif
