#ifndef EXT_VACATION_SETTINGS_H
#define EXT_VACATION_SETTINGS_H

struct ext_vacation_settings {
	pool_t pool;

	unsigned int min_period;
	unsigned int max_period;
	unsigned int default_period;

	const char *default_subject;
	const char *default_subject_template;

	bool use_original_recipient;
	bool check_recipient;
	bool send_from_recipient;
	bool to_header_ignore_envelope;
};

extern const struct setting_parser_info ext_vacation_setting_parser_info;

#endif
