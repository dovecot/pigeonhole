#ifndef EXT_VACATION_COMMON_H
#define EXT_VACATION_COMMON_H

#include "sieve-common.h"

/*
 * Extension configuration
 */

#define EXT_VACATION_DEFAULT_PERIOD (7*24*60*60)
#define EXT_VACATION_DEFAULT_MIN_PERIOD (24*60*60)
#define EXT_VACATION_DEFAULT_MAX_PERIOD 0

struct ext_vacation_context {
	unsigned int min_period;
	unsigned int max_period;
	unsigned int default_period;
	char *default_subject;
	char *default_subject_template;
	bool use_original_recipient;
	bool dont_check_recipient;
	bool send_from_recipient;
	bool to_header_ignore_envelope;
};

/*
 * Commands
 */

extern const struct sieve_command_def vacation_command;

/*
 * Operations
 */

extern const struct sieve_operation_def vacation_operation;

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
