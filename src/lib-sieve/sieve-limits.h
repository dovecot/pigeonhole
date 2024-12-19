#ifndef SIEVE_LIMITS_H
#define SIEVE_LIMITS_H

/*
 * Scripts
 */

#define SIEVE_MAX_SCRIPT_NAME_LEN                       256

#define SIEVE_MAX_LOOP_DEPTH                            4

/*
 * Lexer
 */

#define SIEVE_MAX_STRING_LEN                            (1 << 20)
#define SIEVE_MAX_IDENTIFIER_LEN                        32

/*
 * AST
 */

#define SIEVE_MAX_COMMAND_ARGUMENTS                     32
#define SIEVE_MAX_BLOCK_NESTING                         32
#define SIEVE_MAX_TEST_NESTING                          32

/*
 * Runtime
 */

#define SIEVE_MAX_MATCH_VALUES                          32
#define SIEVE_HIGH_CPU_TIME_MSECS                       1500
#define SIEVE_DEFAULT_RESOURCE_USAGE_TIMEOUT_SECS       

/*
 * Actions
 */

#define SIEVE_MAX_SUBJECT_HEADER_CODEPOINTS             256

#endif
