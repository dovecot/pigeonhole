#ifndef IMAP_FILTER_SIEVE_SETTINGS_H
#define IMAP_FILTER_SIEVE_SETTINGS_H

struct imap_filter_sieve_settings {
	pool_t pool;

	unsigned int max_redirects;
};

extern const struct setting_parser_info imap_filter_sieve_setting_parser_info;

#endif
