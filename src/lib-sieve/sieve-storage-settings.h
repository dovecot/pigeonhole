#ifndef SIEVE_STORAGE_SETTINGS_H
#define SIEVE_STORAGE_SETTINGS_H

#define SIEVE_STORAGE_SETTINGS_FILTER "sieve_script"

struct sieve_storage_settings {
	pool_t pool;

	const char *script_storage;
	unsigned int script_precedence;

	const char *script_type;
	ARRAY_TYPE(const_string) script_cause;

	const char *script_driver;
	const char *script_name;
	const char *script_bin_path;

	uoff_t quota_max_storage;
	unsigned int quota_max_scripts;

	ARRAY_TYPE(const_string) storages;
};

extern const struct setting_parser_info sieve_storage_setting_parser_info;

bool sieve_storage_settings_match_script_type(
	const struct sieve_storage_settings *set, const char *type);
bool sieve_storage_settings_match_script_cause(
	const struct sieve_storage_settings *set, const char *cause);


#endif
