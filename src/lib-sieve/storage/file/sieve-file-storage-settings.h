#ifndef SIEVE_FILE_STORAGE_SETTINGS_H
#define SIEVE_FILE_STORAGE_SETTINGS_H

#define SIEVE_FILE_DEFAULT_ACTIVE_PATH "~/.dovecot."SIEVE_SCRIPT_FILEEXT

struct sieve_file_storage_settings {
	pool_t pool;

	const char *script_path;
	const char *script_active_path;
};

extern const struct setting_parser_info sieve_file_storage_setting_parser_info;

#endif
