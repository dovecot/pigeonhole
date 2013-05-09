/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SIEVE_STORAGE_H
#define __SIEVE_STORAGE_H

#include "lib.h"
#include "mail-storage.h"
#include "mail-user.h"

#include "sieve.h"

#define MAILBOX_ATTRIBUTE_PREFIX_SIEVE \
	MAILBOX_ATTRIBUTE_PREFIX_DOVECOT_PVT"sieve/"
#define MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES \
	MAILBOX_ATTRIBUTE_PREFIX_SIEVE"files/"
#define MAILBOX_ATTRIBUTE_SIEVE_DEFAULT \
	MAILBOX_ATTRIBUTE_PREFIX_SIEVE"default"

#define MAILBOX_ATTRIBUTE_SIEVE_DEFAULT_LINK 'L'
#define MAILBOX_ATTRIBUTE_SIEVE_DEFAULT_SCRIPT 'S'

enum sieve_storage_flags {
	/* Print debugging information */
	SIEVE_STORAGE_FLAG_DEBUG             = 0x01,
	/* This storage is used for synchronization (and not normal ManageSieve)
	 */
	SIEVE_STORAGE_FLAG_SYNCHRONIZING     = 0x02
};

struct sieve_storage *sieve_storage_create
	(struct sieve_instance *svinst, struct mail_user *user, const char *home,
		enum sieve_storage_flags flags);
void sieve_storage_free(struct sieve_storage *storage);

struct sieve_error_handler *sieve_storage_get_error_handler
	(struct sieve_storage *storage);

/* Set error message in storage. Critical errors are logged with i_error(),
   but user sees only "internal error" message. */
void sieve_storage_clear_error(struct sieve_storage *storage);

void sieve_storage_set_error
	(struct sieve_storage *storage, enum sieve_error error,
		const char *fmt, ...) ATTR_FORMAT(3, 4);

void sieve_storage_set_critical(struct sieve_storage *storage,
	const char *fmt, ...) ATTR_FORMAT(2, 3);

const char *sieve_storage_get_last_error
	(struct sieve_storage *storage, enum sieve_error *error_r);

int sieve_storage_get_last_change
	(struct sieve_storage *storage, time_t *last_change_r);
void sieve_storage_set_modified
	(struct sieve_storage *storage, time_t mtime);

#endif
