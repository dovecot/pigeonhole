#ifndef IMAP_SIEVE_PLUGIN_H
#define IMAP_SIEVE_PLUGIN_H

struct module;

extern const char imap_sieve_plugin_binary_dependency[];

void imap_sieve_plugin_init(struct module *module);
void imap_sieve_plugin_deinit(void);

#endif
