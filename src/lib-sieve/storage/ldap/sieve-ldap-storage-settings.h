#ifndef SIEVE_LDAP_STORAGE_SETTINGS_H
#define SIEVE_LDAP_STORAGE_SETTINGS_H

struct sieve_ldap_storage_settings {
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
	const char *debug_level;

	const char *sieve_ldap_script_attr;
	const char *sieve_ldap_mod_attr;
	const char *sieve_ldap_filter;

	/* ... */
	struct {
		int deref, scope, tls_require_cert;
	} parsed;
};

#endif
