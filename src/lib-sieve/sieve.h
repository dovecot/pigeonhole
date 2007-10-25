#ifndef __SIEVE_H
#define __SIEVE_H

#include "sieve-binary.h"

struct sieve_binary *sieve_compile(int fd);
void sieve_dump(struct sieve_binary *binary);
bool sieve_execute(struct sieve_binary *binary); 

#endif
