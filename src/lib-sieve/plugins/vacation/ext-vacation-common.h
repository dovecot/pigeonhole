/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __EXT_VACATION_COMMON_H
#define __EXT_VACATION_COMMON_H

#include "sieve-common.h"

/* 
 * Commands 
 */

extern const struct sieve_command_def vacation_command;

/* 
 * Operations 
 */

extern const struct sieve_operation_def vacation_operation;

/* Extension */

extern const struct sieve_extension_def vacation_extension;

#endif /* __EXT_VACATION_COMMON_H */
