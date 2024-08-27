#ifndef SIEVE_EXTPROGRAMS_SETTINGS_H
#define SIEVE_EXTPROGRAMS_SETTINGS_H

/* <settings checks> */
enum sieve_extprograms_eol {
	SIEVE_EXTPROGRAMS_EOL_CRLF = 0,
	SIEVE_EXTPROGRAMS_EOL_LF
};
/* </settings checks> */

struct sieve_extprograms_settings {
	pool_t pool;

	const char *bin_dir;
	const char *socket_dir;
	const char *input_eol;

	unsigned int exec_timeout;

	struct {
		enum sieve_extprograms_eol input_eol;
	} parsed;
};

extern const struct setting_parser_info sieve_ext_vnd_pipe_setting_parser_info;
extern const struct setting_parser_info sieve_ext_vnd_filter_setting_parser_info;
extern const struct setting_parser_info sieve_ext_vnd_execute_setting_parser_info;

#endif
