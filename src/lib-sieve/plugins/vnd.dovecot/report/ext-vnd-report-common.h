/* Copyright (c)2016 Pigeonhole authors, see the included COPYING file
 */

#ifndef __EXT_REPORT_COMMON_H
#define __EXT_REPORT_COMMON_H

/*
 * Extension
 */

extern const struct sieve_extension_def vnd_report_extension;

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

const char *
ext_vnd_report_parse_feedback_type(const char *feedback_type);

#endif /* __EXT_NOTIFY_COMMON_H */
