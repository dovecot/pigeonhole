#ifndef IMAP_SIEVE_SETTINGS_H
#define IMAP_SIEVE_SETTINGS_H

struct imap_sieve_settings {
	pool_t pool;

	const char *url;
	bool expunge_discarded;
};

extern const struct setting_parser_info imap_sieve_setting_parser_info;

#endif
