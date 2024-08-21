#ifndef EXT_SPAMVIRUSTEST_SETTINGS_H
#define EXT_SPAMVIRUSTEST_SETTINGS_H

/* <settings checks> */
enum ext_spamvirustest_status_type {
	EXT_SPAMVIRUSTEST_STATUS_TYPE_SCORE,
	EXT_SPAMVIRUSTEST_STATUS_TYPE_STRLEN,
	EXT_SPAMVIRUSTEST_STATUS_TYPE_TEXT,
};
/* </settings checks> */

struct ext_spamvirustest_settings {
	pool_t pool;

	const char *status_header;
	const char *status_type;
	const char *max_header;
	const char *max_value;

	ARRAY_TYPE(const_string) text_value;

	struct {
		enum ext_spamvirustest_status_type status_type;

		float max_value;

		const char *text_values[11];
	} parsed;
};

extern const struct setting_parser_info ext_spamtest_setting_parser_info;
extern const struct setting_parser_info ext_virustest_setting_parser_info;

/* <settings checks> */
bool ext_spamvirustest_parse_decimal_value(const char *str_value,
					   float *value_r,
					   const char **error_r);
/* </settings checks> */

#endif
