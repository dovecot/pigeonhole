#ifndef SIEVE_LDAP_STORAGE_SETTINGS_H
#define SIEVE_LDAP_STORAGE_SETTINGS_H

struct sieve_ldap_settings {
	pool_t pool;

	const char *uris;
	const char *auth_dn;
	const char *auth_dn_password;

	bool starttls;
	ARRAY_TYPE(const_string) auth_sasl_mechanisms;
	const char *auth_sasl_realm;
	const char *auth_sasl_authz_id;

	const char *deref;
	const char *scope;
	unsigned int version;

	unsigned int debug_level;

	/* ... */
	struct {
		int deref, scope, tls_require_cert;
	} parsed;
};

/* Settings whose %variables are expanded for each script lookup. They all end
   up either in an LDAP filter or in a DN, so they must be LDAP-escaped. */
struct sieve_ldap_pre_settings {
	pool_t pool;

	const char *base;
	const char *filter;
};

struct sieve_ldap_storage_settings {
	pool_t pool;

	const char *script_attribute;
	const char *modified_attribute;
};

extern const struct setting_parser_info sieve_ldap_setting_parser_info;
extern const struct setting_parser_info sieve_ldap_pre_setting_parser_info;
extern const struct setting_parser_info sieve_ldap_storage_setting_parser_info;

#endif
