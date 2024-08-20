#ifndef SIEVE_SETTINGS_H
#define SIEVE_SETTINGS_H

#include "smtp-address.h"

#include "sieve-config.h"
#include "sieve-address-source.h"

#define SIEVE_SETTINGS_FILTER "sieve"

struct sieve_address_source;

struct sieve_settings {
	pool_t pool;

	bool enabled;

	size_t max_script_size;
	unsigned int max_actions;
	unsigned int max_redirects;
	unsigned int max_cpu_time;
	unsigned int resource_usage_timeout;

	const char* redirect_envelope_from;
	unsigned int redirect_duplicate_period;

	const char *user_email;
	const char *user_log;

	const char *trace_dir;
	const char *trace_level;
	bool trace_debug;
	bool trace_addresses;

	ARRAY_TYPE(const_string) plugins;
	const char *plugin_dir;

	ARRAY_TYPE(const_string) extensions;
	ARRAY_TYPE(const_string) global_extensions;
	ARRAY_TYPE(const_string) implicit_extensions;

	struct {
		struct sieve_address_source redirect_envelope_from;
		const struct smtp_address *user_email;
	} parsed;
};

extern const struct sieve_settings sieve_default_settings;
extern const struct setting_parser_info sieve_setting_parser_info;

#endif
