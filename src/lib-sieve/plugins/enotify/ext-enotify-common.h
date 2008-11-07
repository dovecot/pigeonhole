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
 * Operands
 */

extern const struct sieve_operation notify_operation;

enum ext_variables_opcode {
	EXT_ENOTIFY_OPERATION_NOTIFY
};

#endif /* __EXT_ENOTIFY_COMMON_H */
