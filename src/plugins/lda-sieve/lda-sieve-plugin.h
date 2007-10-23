#ifndef __LDA_SIEVE_PLUGIN_H
#define __LDA_SIEVE_PLUGIN_H

int lda_sieve_run(struct mail_namespace *namespaces,
		  struct mail_storage **storage_r, struct mail *mail,
		  const char *script_path, const char *destaddr,
		  const char *username, const char *mailbox);

void lda_sieve_plugin_init(void);
void lda_sieve_plugin_deinit(void);

#endif
