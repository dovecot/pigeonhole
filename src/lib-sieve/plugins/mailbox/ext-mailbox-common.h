#ifndef EXT_MAILBOX_COMMON_H
#define EXT_MAILBOX_COMMON_H

#include "sieve-common.h"

#include "sieve-ext-mailbox.h"

/*
 * Tagged arguments
 */

extern const struct sieve_argument_def mailbox_create_tag;

/*
 * Commands
 */

extern const struct sieve_command_def mailboxexists_test;

/*
 * Operands
 */

extern const struct sieve_operand_def mailbox_create_operand;

/*
 * Operations
 */

extern const struct sieve_operation_def mailboxexists_operation;

/*
 * Extension
 */

extern const struct sieve_extension_def mailbox_extension;

#endif

