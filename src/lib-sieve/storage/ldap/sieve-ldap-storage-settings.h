#ifndef SIEVE_LDAP_STORAGE_SETTINGS_H
#define SIEVE_LDAP_STORAGE_SETTINGS_H

struct sieve_ldap_settings {
	pool_t pool;

	const char *hosts;
	const char *uris;
	const char *dn;
	const char *dnpass;

	bool tls;
	bool sasl_bind;
	const char *sasl_mech;
	const char *sasl_realm;
	const char *sasl_authz_id;

	const char *tls_ca_cert_file;
	const char *tls_ca_cert_dir;
	const char *tls_cert_file;
	const char *tls_key_file;
	const char *tls_cipher_suite;
	const char *tls_require_cert;

	const char *deref;
	const char *scope;
	const char *base;
	unsigned int ldap_version;

	const char *ldaprc_path;
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
