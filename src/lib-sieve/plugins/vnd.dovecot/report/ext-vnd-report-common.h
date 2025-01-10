#ifndef EXT_REPORT_COMMON_H
#define EXT_REPORT_COMMON_H

/*
 * Extension configuration
 */

struct ext_report_context {
	struct sieve_address_source report_from;
};

/*
 * Extension
 */

extern const struct sieve_extension_def vnd_report_extension;

int ext_report_load(const struct sieve_extension *ext, void **context_r);

/*
 * Commands
 */

extern const struct sieve_command_def cmd_report;

/*
 * Operations
 */

extern const struct sieve_operation_def report_operation;

/*
 * RFC 5965 feedback-type
 */

const char *ext_vnd_report_parse_feedback_type(const char *feedback_type);

#endif
