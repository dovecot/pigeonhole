/* WARNING: THIS FILE IS GENERATED - DO NOT PATCH!
   It's not enough alone in any case, because the defaults may be
   coming from the individual *-settings.c in some situations. If you
   wish to modify defaults, change the other *-settings.c files and
   just delete this file. This file will be automatically regenerated
   by make. (This file is distributed in the tarball only because some
   systems might not have Perl installed.) */
#include "lib.h"
#include "array.h"
#include "str.h"
#include "ipwd.h"
#include "var-expand.h"
#include "file-lock.h"
#include "fsync-mode.h"
#include "hash-format.h"
#include "net.h"
#include "unichar.h"
#include "hash-method.h"
#include "settings.h"
#include "message-header-parser.h"
#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-address-source.h"
#include "managesieve-url.h"
#include "pigeonhole-settings.h"
#include <unistd.h>
#define CONFIG_BINARY
/* ../../../src/lib-sieve/sieve-settings.h */
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

	struct {
		struct sieve_address_source redirect_envelope_from;
		const struct smtp_address *user_email;
	} parsed;
};
/* ../../../src/lib-sieve/sieve-storage-settings.h */
struct sieve_storage_settings {
	pool_t pool;

	const char *script_storage;

	const char *script_type;
	ARRAY_TYPE(const_string) script_cause;

	const char *script_driver;
	const char *script_name;
	const char *script_bin_path;

	uoff_t quota_max_storage;
	unsigned int quota_max_scripts;

	ARRAY_TYPE(const_string) storages;
};
/* ../../../src/lib-sieve/storage/file/sieve-file-storage-settings.h */
#define SIEVE_FILE_DEFAULT_ACTIVE_PATH "~/.dovecot."SIEVE_SCRIPT_FILEEXT
struct sieve_file_storage_settings {
	pool_t pool;

	const char *script_path;
	const char *script_active_path;
};
/* ../../../src/lib-sieve/storage/ldap/sieve-ldap-storage-settings.h */
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
	const char *debug_level;

	/* parsed */
	int parsed_deref, parsed_scope, parsed_tls_require_cert;
};
struct sieve_ldap_storage_settings {
	pool_t pool;

	const char *script_attr;
	const char *mod_attr;
	const char *filter;
};
/* ../../../src/plugins/imapsieve/imap-sieve-settings.h */
struct imap_sieve_settings {
	pool_t pool;

	const char *url;
	bool expunge_discarded;
};
/* ../../../src/plugins/imapsieve/imap-sieve-settings.c */
extern const struct setting_parser_info imap_sieve_setting_parser_info;

/* <settings checks> */
static bool
imap_sieve_settings_check(void *_set, pool_t pool ATTR_UNUSED,
			  const char **error_r)
{
	struct imap_sieve_settings *set = _set;
	const char *error;

	if (*set->url != '\0' &&
	    managesieve_url_parse(set->url, 0, pool_datastack_create(),
				  NULL, &error) < 0) {
		*error_r = t_strdup_printf(
			"Invalid URL for imapsieve_url setting: %s",
			set->url);
		return FALSE;
	}

	return TRUE;
}
/* </settings checks> */
#undef DEF
#define DEF(type, name) \
	SETTING_DEFINE_STRUCT_##type("imapsieve_"#name, name, \
				     struct imap_sieve_settings)
static const struct setting_define imap_sieve_setting_defines[] = {
	{ .type = SET_FILTER_EXTRA, .key = "imapsieve_from",
	  .filter_array_field_name = "sieve_script_imapsieve_from" },

	DEF(STR, url),
	DEF(BOOL, expunge_discarded),

	SETTING_DEFINE_LIST_END,
};
static const struct imap_sieve_settings imap_sieve_default_settings = {
	.url = "",
	.expunge_discarded = FALSE,
};
const struct setting_parser_info imap_sieve_setting_parser_info = {
	.name = "imapsieve",

	.defines = imap_sieve_setting_defines,
	.defaults = &imap_sieve_default_settings,

	.struct_size = sizeof(struct imap_sieve_settings),

	.check_func = imap_sieve_settings_check,

	.pool_offset1 = 1 + offsetof(struct imap_sieve_settings, pool),
};
/* ../../../src/lib-sieve/sieve-settings.c */
extern const struct setting_parser_info sieve_setting_parser_info;
/* ../../../src/lib-sieve/sieve-storage-settings.c */
extern const struct setting_parser_info sieve_storage_setting_parser_info;
/* ../../../src/lib-sieve/storage/file/sieve-file-storage-settings.c */
extern const struct setting_parser_info sieve_file_storage_setting_parser_info;
/* ../../../src/lib-sieve/storage/ldap/sieve-ldap-storage-settings.c */
extern const struct setting_parser_info sieve_ldap_setting_parser_info;
extern const struct setting_parser_info sieve_ldap_storage_setting_parser_info;
const struct setting_parser_info *pigeonhole_settings_set_infos[] = {

	&imap_sieve_setting_parser_info,

	&sieve_file_storage_setting_parser_info,
#if defined(SIEVE_BUILTIN_LDAP) || defined(PLUGIN_BUILD)
	&sieve_ldap_setting_parser_info,
#endif
#if defined(SIEVE_BUILTIN_LDAP) || defined(PLUGIN_BUILD)
	&sieve_ldap_storage_setting_parser_info,
#endif

	&sieve_setting_parser_info,

	&sieve_storage_setting_parser_info,
	NULL
};
