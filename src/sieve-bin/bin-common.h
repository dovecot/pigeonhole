#ifndef __BIN_COMMON_H
#define __BIN_COMMON_H

#include "sieve.h"

/* Functionality common to all sieve test binaries */

void bin_init(void);
void bin_deinit(void);

const char *bin_get_user(void);

struct sieve_binary *bin_compile_sieve_script(const char *filename);
struct sieve_binary *bin_open_sieve_script(const char *filename);
void bin_dump_sieve_binary_to(struct sieve_binary *sbin, const char *filename);

int bin_open_mail_file(const char *filename);
void bin_close_mail_file(int mfd);

void bin_fill_in_envelope
	(struct mail *mail, const char **recipient, const char **sender);

#endif /* __BIN_COMMON_H */
