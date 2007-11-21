#ifndef __SIEVE_H
#define __SIEVE_H

#include "lib.h"
#include "mail-storage.h"

struct sieve_binary;

struct sieve_message_data {
	struct mail *mail;
	const char *return_path;
	const char *to_address;
	const char *auth_user;
};	

bool sieve_init(const char *plugins);
void sieve_deinit(void);

struct sieve_binary *sieve_compile(int fd);
void sieve_dump(struct sieve_binary *binary);
bool sieve_execute
	(struct sieve_binary *binary, struct sieve_message_data *msgdata);

#endif
