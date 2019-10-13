#ifndef EXT_SPECIAL_USE_COMMON_H
#define EXT_SPECIAL_USE_COMMON_H

#include "sieve-common.h"

/*
 * Tagged arguments
 */

extern const struct sieve_argument_def specialuse_tag;

/*
 * Commands
 */

extern const struct sieve_command_def specialuse_exists_test;

/*
 * Operands
 */

extern const struct sieve_operand_def specialuse_operand;

/*
 * Operations
 */

extern const struct sieve_operation_def specialuse_exists_operation;

/*
 * Extension
 */

extern const struct sieve_extension_def special_use_extension;

/*
 * Flag checking
 */

bool ext_special_use_flag_valid(const char *flag);

#endif /* EXT_SPECIAL_USE_COMMON_H */

