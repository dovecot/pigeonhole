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
	const char *base;
	unsigned int version;

	unsigned int debug_level;

	/* ... */
	struct {
		int deref, scope, tls_require_cert;
	} parsed;
};

struct sieve_ldap_storage_settings {
	pool_t pool;

	const char *script_attr;
	const char *mod_attr;
	const char *filter;
};

extern const struct setting_parser_info sieve_ldap_setting_parser_info;
extern const struct setting_parser_info sieve_ldap_storage_setting_parser_info;

#endif
