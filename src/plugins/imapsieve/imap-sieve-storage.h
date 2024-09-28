#ifndef IMAP_SIEVE_STORAGE_H
#define IMAP_SIEVE_STORAGE_H

void imap_sieve_storage_init(struct module *module);
void imap_sieve_storage_deinit(void);

void imap_sieve_storage_client_created(struct client *client);

#endif
