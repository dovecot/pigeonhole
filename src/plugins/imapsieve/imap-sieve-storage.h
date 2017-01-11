/* Copyright (c) 2016-2017 Pigeonhole authors, see the included COPYING file
 */

#ifndef __IMAP_SIEVE_STORAGE_H
#define __IMAP_SIEVE_STORAGE_H

void imap_sieve_storage_init(struct module *module);
void imap_sieve_storage_deinit(void);

void imap_sieve_storage_client_created(struct client *client,
	bool user_script);

#endif
