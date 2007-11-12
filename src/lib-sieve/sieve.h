#ifndef __SIEVE_H
#define __SIEVE_H

#include "mail-storage.h"

struct sieve_binary;

bool sieve_init(const char *plugins);
void sieve_deinit(void);

struct sieve_binary *sieve_compile(int fd);
void sieve_dump(struct sieve_binary *binary);
bool sieve_execute(struct sieve_binary *binary, struct mail *mail);

#endif
