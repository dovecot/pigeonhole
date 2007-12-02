#ifndef __BIN_COMMON_H
#define __BIN_COMMON_H

#include "sieve.h"

void bin_init(void);
void bin_deinit(void);

const char *bin_get_user(void);
struct sieve_binary *bin_compile_sieve_script(const char *filename);
void bin_dump_sieve_binary_to(struct sieve_binary *sbin, const char *filename);

#endif /* __BIN_COMMON_H */
