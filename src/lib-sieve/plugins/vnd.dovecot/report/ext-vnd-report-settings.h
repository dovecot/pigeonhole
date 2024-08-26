#ifndef EXT_REPORT_SETTINGS_H
#define EXT_REPORT_SETTINGS_H

#include "sieve-address-source.h"

struct ext_report_settings {
	pool_t pool;

	const char *from;

	struct {
		struct sieve_address_source from;
	} parsed;
};

extern const struct setting_parser_info ext_report_setting_parser_info;

#endif
