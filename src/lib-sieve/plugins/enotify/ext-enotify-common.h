/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __EXT_ENOTIFY_COMMON_H
#define __EXT_ENOTIFY_COMMON_H

/*
 * Extension
 */

extern const struct sieve_extension enotify_extension;

/*
 * Commands
 */

extern const struct sieve_command notify_command;

/*
 * Tests
 */

extern const struct sieve_command valid_notify_method_test;
extern const struct sieve_command notify_method_capability_test;

/*
 * Operands
 */

extern const struct sieve_operation notify_operation;
extern const struct sieve_operation valid_notify_method_operation;
extern const struct sieve_operation notify_method_capability_operation;

enum ext_variables_opcode {
	EXT_ENOTIFY_OPERATION_NOTIFY,
	EXT_ENOTIFY_OPERATION_VALID_NOTIFY_METHOD,
	EXT_ENOTIFY_OPERATION_NOTIFY_METHOD_CAPABILITY
};

#endif /* __EXT_ENOTIFY_COMMON_H */
