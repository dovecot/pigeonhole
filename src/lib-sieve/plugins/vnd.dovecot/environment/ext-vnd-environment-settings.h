#ifndef EXT_VND_ENVIRONMENT_SETTINGS_H
#define EXT_VND_ENVIRONMENT_SETTINGS_H

struct ext_vnd_environment_settings {
	pool_t pool;

	ARRAY_TYPE(const_string) envs;
};

extern const struct setting_parser_info ext_vnd_environment_setting_parser_info;

#endif
