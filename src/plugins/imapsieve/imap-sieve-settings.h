#ifndef IMAP_SIEVE_SETTINGS_H
#define IMAP_SIEVE_SETTINGS_H

struct imap_sieve_settings {
	pool_t pool;

	const char *url;
	bool expunge_discarded;

	ARRAY_TYPE(const_string) from;
	const char *from_name;
};

extern const struct setting_parser_info imap_sieve_setting_parser_info;

#endif
