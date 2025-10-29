#ifndef EXT_EXTLISTS_SETTINGS_H
#define EXT_EXTLISTS_SETTINGS_H

/* <settings checks> */
#define SIEVE_URN_PREFIX "urn:ietf:params:sieve"
#define SIEVE_URN_ADDRBOOK SIEVE_URN_PREFIX":addrbook"
#define SIEVE_URN_ADDRBOOK_DEFAULT SIEVE_URN_ADDRBOOK":default"
/* </settings checks> */

struct ext_extlists_list_settings {
	pool_t pool;

	const char *name;
	/* Maximum size of lookup value */
	uoff_t max_lookup_size;

	struct {
		const char *name;
	} parsed;
};

struct ext_extlists_settings {
	pool_t pool;

	ARRAY_TYPE(const_string) lists;
};

extern const struct setting_parser_info ext_extlists_list_setting_parser_info;
extern const struct setting_parser_info ext_extlists_setting_parser_info;

/* <settings checks> */
int ext_extlists_name_normalize(const char **name, const char **error_r);
/* </settings checks> */

#endif
