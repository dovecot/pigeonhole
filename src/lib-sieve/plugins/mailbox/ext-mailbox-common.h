/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __EXT_MAILBOX_COMMON_H
#define __EXT_MAILBOX_COMMON_H

#include "sieve-common.h"

/*
 * Tagged arguments
 */

extern const struct sieve_argument mailbox_create_tag;

/*
 * Commands
 */

extern const struct sieve_command mailboxexists_test;

/*
 * Operands
 */

extern const struct sieve_operand mailbox_create_operand;

/*
 * Operations
 */

extern const struct sieve_operation mailboxexists_operation;

/*
 * Extension
 */

extern const struct sieve_extension mailbox_extension;

#endif /* __EXT_MAILBOX_COMMON_H */

