#ifndef MANAGESIEVE_SETTINGS_H
#define MANAGESIEVE_SETTINGS_H

struct mail_user_settings;

/* <settings checks> */
enum managesieve_client_workarounds {
	WORKAROUND_NONE = 0x00
};
/* </settings checks> */

struct managesieve_settings {
	pool_t pool;
	bool verbose_proctitle;
	const char *rawlog_dir;

	/* managesieve: */
	uoff_t managesieve_max_line_length;
	const char *managesieve_implementation_string;
	const char *managesieve_client_workarounds;
	const char *managesieve_logout_format;
	unsigned int managesieve_max_compile_errors;

	enum managesieve_client_workarounds parsed_workarounds;
};

struct plugin_settings {
	pool_t pool;
	ARRAY(const char *) plugin_envs;
};

extern const struct setting_parser_info managesieve_setting_parser_info;
extern const struct setting_parser_info managesieve_plugin_setting_parser_info;

#endif
