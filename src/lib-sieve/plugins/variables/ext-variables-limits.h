#ifndef EXT_VARIABLES_LIMITS_H
#define EXT_VARIABLES_LIMITS_H

#include "sieve-limits.h"

/* From RFC 5229:
 *
 * 6.  Implementation Limits
 *
 *  An implementation of this document MUST support at least 128 distinct
 *  variables.  The supported length of variable names MUST be at least
 *  32 characters.  Each variable MUST be able to hold at least 4000
 *  characters.  Attempts to set the variable to a value larger than what
 *  the implementation supports SHOULD be reported as an error at
 *  compile-time if possible.  If the attempt is discovered during run-
 *  time, the value SHOULD be truncated, and it MUST NOT be treated as an
 *  error.

 *  Match variables ${1} through ${9} MUST be supported.  References to
 *  higher indices than those the implementation supports MUST be treated
 *  as a syntax error, which SHOULD be discovered at compile-time.
 */

#define EXT_VARIABLES_REQUIRED_MAX_SCOPE_COUNT   128
#define EXT_VARIABLES_REQUIRED_MAX_VALUE_SIZE    4000

#define EXT_VARIABLES_MAX_VARIABLE_NAME_LEN      64
#define EXT_VARIABLES_MAX_NAMESPACE_ELEMENTS     10

#define EXT_VARIABLES_MAX_MATCH_INDEX            SIEVE_MAX_MATCH_VALUES

#endif
