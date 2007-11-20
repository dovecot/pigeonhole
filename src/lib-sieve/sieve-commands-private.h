#ifndef __SIEVE_COMMANDS_PRIVATE_H
#define __SIEVE_COMMANDS_PRIVATE_H

#include "sieve-common.h"
#include "sieve-commands.h"

/* Core commands */

extern const struct sieve_command cmd_require;
extern const struct sieve_command cmd_if;
extern const struct sieve_command cmd_elsif;
extern const struct sieve_command cmd_else;
extern const struct sieve_command cmd_redirect;

/* Core tests */

extern const struct sieve_command tst_address;
extern const struct sieve_command tst_header;
extern const struct sieve_command tst_exists;
extern const struct sieve_command tst_size;
extern const struct sieve_command tst_not;
extern const struct sieve_command tst_anyof;
extern const struct sieve_command tst_allof;

/* Lists */

extern const struct sieve_command *sieve_core_commands[];
extern const unsigned int sieve_core_commands_count;

extern const struct sieve_command *sieve_core_tests[];
extern const unsigned int sieve_core_tests_count;

#endif /* __SIEVE_COMMANDS_PRIVATE_H */

