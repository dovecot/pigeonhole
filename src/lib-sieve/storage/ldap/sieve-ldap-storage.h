#ifndef SIEVE_LDAP_STORAGE_H
#define SIEVE_LDAP_STORAGE_H

#include "sieve.h"
#include "sieve-script-private.h"
#include "sieve-storage-private.h"

#define SIEVE_LDAP_SCRIPT_DEFAULT "default"

#if defined(SIEVE_BUILTIN_LDAP) || defined(PLUGIN_BUILD)

#include "sieve-ldap-storage-settings.h"
#include "sieve-ldap-db.h"

struct sieve_ldap_storage;

/*
 * LDAP settings
 */

int sieve_ldap_storage_read_settings(struct sieve_ldap_storage *lstorage,
				     const char *config_path);

/*
 * Storage class
 */

struct sieve_ldap_storage {
	struct sieve_storage storage;

	struct sieve_ldap_storage_settings set;
	time_t set_mtime;

	const char *config_file;

	struct ldap_connection *conn;
};

int sieve_ldap_storage_active_script_get_name(struct sieve_storage *storage,
					      const char **name_r);

/*
 * Script class
 */

struct sieve_ldap_script {
	struct sieve_script script;

	const char *dn;
	const char *modattr;

	const char *bin_path;
};

struct sieve_ldap_script *
sieve_ldap_script_init(struct sieve_ldap_storage *lstorage, const char *name);

/*
 * Script sequence
 */

int sieve_ldap_script_sequence_init(struct sieve_script_sequence *sseq,
				    enum sieve_error *error_code_r);
int sieve_ldap_script_sequence_next(struct sieve_script_sequence *sseq,
				    struct sieve_script **script_r,
				    enum sieve_error *error_code_r);
void sieve_ldap_script_sequence_destroy(struct sieve_script_sequence *sseq);

#endif

#endif
