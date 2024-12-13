/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "env-util.h"
#include "settings-parser.h"

#include "sieve-common.h"

#include "sieve-ldap-storage.h"
#include "sieve-ldap-storage-settings.h"

#if defined(STORAGE_LDAP) && !defined(PLUGIN_BUILD)

#include "sieve-error.h"

#include "sieve-ldap-db.h"

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("ldap_"#name, name, \
				     struct sieve_ldap_settings)

static bool
sieve_ldap_settings_check(void *_set, pool_t pool, const char **error_r);

static const struct setting_define sieve_ldap_setting_defines[] = {
	DEF(STR, uris),
	DEF(STR, auth_dn),
	DEF(STR, auth_dn_password),
	DEF(BOOL, starttls),
	DEF(BOOL, sasl_bind),
	DEF(STR, sasl_mech),
	DEF(STR, sasl_realm),
	DEF(STR, sasl_authz_id),
	DEF(STR, tls_ca_cert_file),
	DEF(STR, tls_ca_cert_dir),
	DEF(STR, tls_cert_file),
	DEF(STR, tls_key_file),
	DEF(STR, tls_cipher_suite),
	DEF(STR, tls_require_cert),
	DEF(ENUM, deref),
	DEF(ENUM, scope),
	DEF(STR, base),
	DEF(UINT, ldap_version),
	DEF(UINT, debug_level),
	DEF(STR, ldaprc_path),

	SETTING_DEFINE_LIST_END
};

const struct sieve_ldap_settings sieve_ldap_default_settings = {
	.uris = "",
	.auth_dn = "",
	.auth_dn_password = "",
	.starttls = FALSE,
	.sasl_bind = FALSE,
	.sasl_mech = "",
	.sasl_realm = "",
	.sasl_authz_id = "",
	.tls_ca_cert_file = "",
	.tls_ca_cert_dir = "",
	.tls_cert_file = "",
	.tls_key_file = "",
	.tls_cipher_suite = "",
	.tls_require_cert = "",
	.deref = "never:searching:finding:always",
	.scope = "subtree:onelevel:base",
	.base = "",
	.ldap_version = 3,
	.debug_level = 0,
	.ldaprc_path = "",
};

const struct setting_parser_info sieve_ldap_setting_parser_info = {
	.name = "sieve_ldap",
	.defines = sieve_ldap_setting_defines,
	.defaults = &sieve_ldap_default_settings,

	.pool_offset1 = 1 + offsetof(struct sieve_ldap_settings, pool),
	.struct_size = sizeof(struct sieve_ldap_settings),
	.check_func = sieve_ldap_settings_check,
};

#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("sieve_script_ldap_"#name, name, \
				     struct sieve_ldap_storage_settings)

static const struct setting_define sieve_ldap_storage_setting_defines[] = {
	DEF(STR, script_attr),
	DEF(STR, mod_attr),
	DEF(STR, filter),

	SETTING_DEFINE_LIST_END
};

static struct sieve_ldap_storage_settings sieve_ldap_storage_server_default_settings = {
	.script_attr = "mailSieveRuleSource",
	.mod_attr = "modifyTimestamp",
	.filter = "(&(objectClass=posixAccount)(uid=%u))",
};

const struct setting_parser_info sieve_ldap_storage_setting_parser_info = {
	.name = "sieve_ldap_storage",

	.defines = sieve_ldap_storage_setting_defines,
	.defaults = &sieve_ldap_storage_server_default_settings,

	.pool_offset1 = 1 + offsetof(struct sieve_ldap_storage_settings, pool),
	.struct_size = sizeof(struct sieve_ldap_storage_settings),
};

/* <settings checks> */
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
sieve_ldap_settings_check(void *_set, pool_t pool ATTR_UNUSED,
			  const char **error_r)
{
	struct sieve_ldap_settings *set = _set;
	const char *str;

	if (set->base[0] == '\0' &&
	    settings_get_config_binary() == SETTINGS_BINARY_OTHER) {
		*error_r = "ldap: No search base given";
		return FALSE;
	}

	if (*set->ldaprc_path != '\0') {
		str = getenv("LDAPRC");
		if (str != NULL && strcmp(str, set->ldaprc_path) != 0) {
			*error_r = t_strdup_printf("ldap: "
				"Multiple different ldaprc_path settings not allowed "
				"(%s and %s)", str, set->ldaprc_path);
			return FALSE;
		}
		env_put("LDAPRC", set->ldaprc_path);
	}

	if (ldap_deref_from_str(set->deref, &set->parsed.deref) < 0) {
		*error_r = t_strdup_printf("ldap: "
			"Invalid deref option '%s'", set->deref);
		return FALSE;
	}

	if (ldap_scope_from_str(set->scope, &set->parsed.scope) < 0) {
		*error_r = t_strdup_printf("ldap: "
			"Invalid scope option '%s'", set->scope);
		return FALSE;
	}

#ifdef OPENLDAP_TLS_OPTIONS
	if (*set->tls_require_cert != '\0' &&
	    ldap_tls_require_cert_from_str(
		set->tls_require_cert,
		&set->parsed.tls_require_cert) < 0) {
		*error_r = t_strdup_printf("ldap: "
			"Invalid tls_require_cert option '%s'",
			set->tls_require_cert);
		return FALSE;
	}
#endif

	return TRUE;
}
/* </settings checks> */

#endif
