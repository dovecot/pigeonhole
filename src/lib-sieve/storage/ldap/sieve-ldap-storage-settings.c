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
	DEF(BOOLLIST, auth_sasl_mechanisms),
	DEF(STR, auth_sasl_realm),
	DEF(STR, auth_sasl_authz_id),
	DEF(ENUM, deref),
	DEF(ENUM, scope),
	DEF(STR, base),
	DEF(UINT, version),
	DEF(UINT, debug_level),

	SETTING_DEFINE_LIST_END
};

const struct sieve_ldap_settings sieve_ldap_default_settings = {
	.uris = "",
	.auth_dn = "",
	.auth_dn_password = "",
	.starttls = FALSE,
	.auth_sasl_mechanisms = ARRAY_INIT,
	.auth_sasl_realm = "",
	.auth_sasl_authz_id = "",
	.deref = "never:searching:finding:always",
	.scope = "subtree:onelevel:base",
	.base = "",
	.version = 3,
	.debug_level = 0,
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

static bool
sieve_ldap_settings_check(void *_set, pool_t pool ATTR_UNUSED,
			  const char **error_r)
{
	struct sieve_ldap_settings *set = _set;

	if (set->base[0] == '\0' &&
	    settings_get_config_binary() == SETTINGS_BINARY_OTHER) {
		*error_r = "ldap: No search base given";
		return FALSE;
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

	return TRUE;
}
/* </settings checks> */

#endif
