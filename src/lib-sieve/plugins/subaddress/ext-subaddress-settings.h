#ifndef EXT_SUBADDRESS_SETTINGS_H
#define EXT_SUBADDRESS_SETTINGS_H

struct ext_subaddress_settings {
	pool_t pool;

	const char *recipient_delimiter;
};

extern const struct setting_parser_info ext_subaddress_setting_parser_info;

#endif
