/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "env-util.h"
#include "settings-legacy.h"

#include "sieve-common.h"

#include "sieve-ldap-storage.h"

#if defined(SIEVE_BUILTIN_LDAP) || defined(PLUGIN_BUILD)

#include "sieve-error.h"

#include "sieve-ldap-db.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define DEF_STR(name) DEF_STRUCT_STR(name, sieve_ldap_storage_settings)
#define DEF_INT(name) DEF_STRUCT_INT(name, sieve_ldap_storage_settings)
#define DEF_BOOL(name) DEF_STRUCT_BOOL(name, sieve_ldap_storage_settings)

static struct setting_def setting_defs[] = {
	DEF_STR(hosts),
	DEF_STR(uris),
	DEF_STR(dn),
	DEF_STR(dnpass),
	DEF_BOOL(tls),
	DEF_BOOL(sasl_bind),
	DEF_STR(sasl_mech),
	DEF_STR(sasl_realm),
	DEF_STR(sasl_authz_id),
	DEF_STR(tls_ca_cert_file),
	DEF_STR(tls_ca_cert_dir),
	DEF_STR(tls_cert_file),
	DEF_STR(tls_key_file),
	DEF_STR(tls_cipher_suite),
	DEF_STR(tls_require_cert),
	DEF_STR(deref),
	DEF_STR(scope),
	DEF_STR(base),
	DEF_INT(ldap_version),
	DEF_STR(debug_level),
	DEF_STR(ldaprc_path),
	DEF_STR(sieve_ldap_script_attr),
	DEF_STR(sieve_ldap_mod_attr),
	DEF_STR(sieve_ldap_filter),

	{ 0, NULL, 0 }
};

static struct sieve_ldap_storage_settings default_settings = {
	.hosts = NULL,
	.uris = NULL,
	.dn = NULL,
	.dnpass = NULL,
	.tls = FALSE,
	.sasl_bind = FALSE,
	.sasl_mech = NULL,
	.sasl_realm = NULL,
	.sasl_authz_id = NULL,
	.tls_ca_cert_file = NULL,
	.tls_ca_cert_dir = NULL,
	.tls_cert_file = NULL,
	.tls_key_file = NULL,
	.tls_cipher_suite = NULL,
	.tls_require_cert = NULL,
	.deref = "never",
	.scope = "subtree",
	.base = NULL,
	.ldap_version = 3,
	.debug_level = "0",
	.ldaprc_path = "",
	.sieve_ldap_script_attr = "mailSieveRuleSource",
	.sieve_ldap_mod_attr = "modifyTimestamp",
	.sieve_ldap_filter = "(&(objectClass=posixAccount)(uid=%u))",
};

static const char *
parse_setting(const char *key, const char *value,
	      struct sieve_ldap_storage *lstorage)
{
	return parse_setting_from_defs(lstorage->storage.pool, setting_defs,
				       lstorage->set, key, value);
}

static int ldap_deref_from_str(const char *str, int *deref_r)
{
	if (strcasecmp(str, "never") == 0)
		*deref_r = LDAP_DEREF_NEVER;
	else if (strcasecmp(str, "searching") == 0)
		*deref_r = LDAP_DEREF_SEARCHING;
	else if (strcasecmp(str, "finding") == 0)
		*deref_r = LDAP_DEREF_FINDING;
	else if (strcasecmp(str, "always") == 0)
		*deref_r = LDAP_DEREF_ALWAYS;
	else
		return -1;
	return 0;
}

static int ldap_scope_from_str(const char *str, int *scope_r)
{
	if (strcasecmp(str, "base") == 0)
		*scope_r = LDAP_SCOPE_BASE;
	else if (strcasecmp(str, "onelevel") == 0)
		*scope_r = LDAP_SCOPE_ONELEVEL;
	else if (strcasecmp(str, "subtree") == 0)
		*scope_r = LDAP_SCOPE_SUBTREE;
	else
		return -1;
	return 0;
}

#ifdef OPENLDAP_TLS_OPTIONS
static int ldap_tls_require_cert_from_str(const char *str, int *opt_x_tls_r)
{
	if (strcasecmp(str, "never") == 0)
		*opt_x_tls_r = LDAP_OPT_X_TLS_NEVER;
	else if (strcasecmp(str, "hard") == 0)
		*opt_x_tls_r = LDAP_OPT_X_TLS_HARD;
	else if (strcasecmp(str, "demand") == 0)
		*opt_x_tls_r = LDAP_OPT_X_TLS_DEMAND;
	else if (strcasecmp(str, "allow") == 0)
		*opt_x_tls_r = LDAP_OPT_X_TLS_ALLOW;
	else if (strcasecmp(str, "try") == 0)
		*opt_x_tls_r = LDAP_OPT_X_TLS_TRY;
	else
		return -1;
	return 0;
}
#endif

static bool
sieve_ldap_settings_check(struct sieve_ldap_storage_settings *set,
			  const char **error_r)
{
	const char *str;

	if (set->base == NULL) {
		*error_r = "No search base given";
		return FALSE;
	}

	if (set->uris == NULL && set->hosts == NULL) {
		*error_r = "No uris or hosts set";
		return FALSE;
	}

	if (*set->ldaprc_path != '\0') {
		str = getenv("LDAPRC");
		if (str != NULL && strcmp(str, set->ldaprc_path) != 0) {
			*error_r = t_strdup_printf(
				"Multiple different ldaprc_path settings not allowed "
				"(%s and %s)", str, set->ldaprc_path);
			return FALSE;
		}
		env_put("LDAPRC", set->ldaprc_path);
	}

	if (ldap_deref_from_str(set->deref, &set->ldap_deref) < 0) {
		*error_r = t_strdup_printf(
			"Invalid deref option '%s'", set->deref);
		return FALSE;
	}

	if (ldap_scope_from_str(set->scope, &set->ldap_scope) < 0) {
		*error_r = t_strdup_printf(
			"Invalid scope option '%s'", set->scope);
		return FALSE;
	}

#ifdef OPENLDAP_TLS_OPTIONS
	if (set->tls_require_cert != NULL &&
	    ldap_tls_require_cert_from_str(
		set->tls_require_cert,
		&set->ldap_tls_require_cert) < 0) {
		*error_r = t_strdup_printf(
			"Invalid tls_require_cert option '%s'",
			set->tls_require_cert);
		return FALSE;
	}
#endif

	return TRUE;
}

int sieve_ldap_storage_read_settings(struct sieve_ldap_storage *lstorage,
				     const char *config_path)
{
	struct sieve_storage *storage = &lstorage->storage;
	const char *error;
	struct stat st;

	if (stat(config_path, &st) < 0) {
		sieve_storage_set_critical(storage,
			"Failed to read LDAP storage config: "
			"stat(%s) failed: %m", config_path);
		return -1;
	}

	lstorage->set = p_new(storage->pool,
			      struct sieve_ldap_storage_settings, 1);
	*lstorage->set = default_settings;
	lstorage->set_mtime = st.st_mtime;

	if (!settings_read_nosection(config_path, parse_setting, lstorage,
				     &error)) {
		sieve_storage_set_critical(storage,
			"Failed to read LDAP storage config '%s': %s",
			config_path, error);
		return -1;
	}

	if (!sieve_ldap_settings_check(lstorage->set, &error)) {
		sieve_storage_set_critical(storage,
			"Invalid LDAP storage config '%s': %s",
			config_path, error);
		return -1;
	}
	return 0;
}

#endif
