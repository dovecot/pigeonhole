#ifndef MANAGESIEVE_LOGIN_SETTINGS_H
#define MANAGESIEVE_LOGIN_SETTINGS_H

struct managesieve_login_settings {
	pool_t pool;
	const char *managesieve_implementation_string;
	ARRAY_TYPE(const_string) managesieve_sieve_capability;
	ARRAY_TYPE(const_string) managesieve_notify_capability;
};

extern const struct setting_parser_info managesieve_login_setting_parser_info;

#ifdef _CONFIG_PLUGIN
void managesieve_login_settings_init(void);
void managesieve_login_settings_deinit(void);
#endif

#endif
