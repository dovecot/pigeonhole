#ifndef EXT_ENOTIFY_SETTINGS_H
#define EXT_ENOTIFY_SETTINGS_H

struct ext_enotify_settings {
	pool_t pool;

	unsigned int max_notifications;
};

extern const struct setting_parser_info ext_enotify_setting_parser_info;

#endif
