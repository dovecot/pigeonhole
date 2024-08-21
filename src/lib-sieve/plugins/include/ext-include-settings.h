#ifndef EXT_INCLUDE_SETTINGS_H
#define EXT_INCLUDE_SETTINGS_H

struct ext_include_settings {
	pool_t pool;

	unsigned int max_nesting_depth;
	unsigned int max_includes;
};

extern const struct setting_parser_info ext_include_setting_parser_info;

#endif
