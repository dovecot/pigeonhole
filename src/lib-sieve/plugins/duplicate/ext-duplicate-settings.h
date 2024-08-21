#ifndef EXT_DUPLICATE_SETTINGS_H
#define EXT_DUPLICATE_SETTINGS_H

struct ext_duplicate_settings {
	pool_t pool;

	unsigned int default_period;
	unsigned int max_period;
};

extern const struct setting_parser_info ext_duplicate_setting_parser_info;

#endif
