/* Copyright (c) 2002-2007 Dovecot authors, see the included COPYING file */

#ifndef __SIEVE_H
#define __SIEVE_H

#include "lib.h"
#include "mail-storage.h"

#include <stdio.h>

#include "sieve-error.h"

#define SIEVE_VERSION "0.0.1"
#define SIEVE_IMPLEMENTATION "Dovecot Sieve " SIEVE_VERSION

struct sieve_binary;

struct sieve_message_data {
	struct mail *mail;
	const char *return_path;
	const char *to_address;
	const char *auth_user;
	const char *id;
};

struct sieve_mail_environment {
	const char *inbox;
	struct mail_namespace *namespaces;
	
	const char *username;
	const char *hostname;
	const char *postmaster_address;
	
	/* Callbacks */
	
	/* Interface for sending mail */
	void *(*smtp_open)
		(const char *destination, const char *return_path, FILE **file_r);
	bool (*smtp_close)(void *handle);
	
	/* Interface for marking and checking duplicates */
	int (*duplicate_check)(const void *id, size_t id_size, const char *user);
	void (*duplicate_mark)(const void *id, size_t id_size,
                    const char *user, time_t time);
};	

bool sieve_init(const char *plugins);
void sieve_deinit(void);

struct sieve_binary *sieve_compile
	(const char *scriptpath, struct sieve_error_handler *ehandler);

void sieve_dump(struct sieve_binary *sbin, struct ostream *stream);
bool sieve_test
	(struct sieve_binary *sbin, const struct sieve_message_data *msgdata, 
		const struct sieve_mail_environment *menv, 
		struct sieve_error_handler *ehandler);

bool sieve_execute
	(struct sieve_binary *sbin, const struct sieve_message_data *msgdata,
		const struct sieve_mail_environment *menv,
		struct sieve_error_handler *ehandler);

#endif
