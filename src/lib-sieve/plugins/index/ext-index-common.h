#ifndef EXT_INDEX_COMMON_H
#define EXT_INDEX_COMMON_H

#include "sieve-common.h"

#include <time.h>

#define SIEVE_EXT_INDEX_HDR_OVERRIDE_SEQUENCE 100

/*
 * Tagged arguments
 */

extern const struct sieve_argument_def index_tag;
extern const struct sieve_argument_def last_tag;

/*
 * Operands
 */

extern const struct sieve_operand_def index_operand;

/*
 * Extension
 */

extern const struct sieve_extension_def index_extension;

#endif
