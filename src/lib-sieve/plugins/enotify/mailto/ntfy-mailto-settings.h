#ifndef NTFY_MAILTO_SETTINGS_H
#define NTFY_MAILTO_SETTINGS_H

#include "sieve-address-source.h"

struct ntfy_mailto_settings {
	pool_t pool;

	const char *envelope_from;

	struct {
		struct sieve_address_source envelope_from;
	} parsed;
};

extern const struct setting_parser_info ntfy_mailto_setting_parser_info;

#endif
