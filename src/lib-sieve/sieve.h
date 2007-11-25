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
	const char *id;
};

struct sieve_mail_environment {
	/* Interface for sending mail (callbacks if you like) */
	int (*send_rejection)
		(const struct sieve_message_data *msgdata, const char *recipient, 
			const char *reason);
	int (*send_forward)
		(const struct sieve_message_data *msgdata, const char *forwardto);
};	

bool sieve_init(const char *plugins);
void sieve_deinit(void);

struct sieve_binary *sieve_compile(int fd, bool verbose);
void sieve_dump(struct sieve_binary *binary);
bool sieve_test
	(struct sieve_binary *binary, const struct sieve_message_data *msgdata); 
bool sieve_execute
	(struct sieve_binary *binary, const struct sieve_message_data *msgdata,
		const struct sieve_mail_environment *menv);

#endif
