#ifndef IMAP_FILTER_SIEVE_PLUGIN_H
#define IMAP_FILTER_SIEVE_PLUGIN_H

struct module;

extern const char imap_filter_sieve_plugin_binary_dependency[];

bool cmd_filter(struct client_command_context *cmd);

void imap_filter_sieve_plugin_init(struct module *module);
void imap_filter_sieve_plugin_deinit(void);

#endif
