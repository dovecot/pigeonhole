#ifndef EXT_EDITHEADER_SETTINGS_H
#define EXT_EDITHEADER_SETTINGS_H

struct ext_editheader_header_settings {
	pool_t pool;
	const char *name;

	bool forbid_add;
	bool forbid_delete;
};

struct ext_editheader_settings {
	pool_t pool;

	uoff_t max_header_size;
	ARRAY_TYPE(const_string) headers;
};

extern const struct setting_parser_info ext_editheader_header_setting_parser_info;
extern const struct setting_parser_info ext_editheader_setting_parser_info;

#endif
