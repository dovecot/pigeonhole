/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __EXT_NOTIFY_COMMON_H
#define __EXT_NOTIFY_COMMON_H

/*
 * Extension
 */

extern const struct sieve_extension notify_extension;

/*
 * Commands
 */

extern const struct sieve_command cmd_notify_old;
extern const struct sieve_command cmd_denotify;

/*
 * Operations
 */

extern const struct sieve_operation notify_old_operation;
extern const struct sieve_operation denotify_operation;

enum ext_notify_opcode {
	EXT_NOTIFY_OPERATION_NOTIFY,
	EXT_NOTIFY_OPERATION_DENOTIFY,
};

/* Action context */

struct ext_notify_recipient {
	const char *full;
	const char *normalized;
};

ARRAY_DEFINE_TYPE(recipients, struct ext_notify_recipient);
		
struct ext_notify_action {
	const char *id;
	const char *message;
	sieve_number_t importance;

	ARRAY_TYPE(recipients) recipients;
};

#endif /* __EXT_NOTIFY_COMMON_H */
