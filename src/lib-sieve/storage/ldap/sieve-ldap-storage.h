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
 * Storage class
 */

struct sieve_ldap_storage {
	struct sieve_storage storage;

	const struct sieve_ldap_settings *ldap_set;
	const struct sieve_ldap_storage_settings *set;
	const struct ssl_settings *ssl_set;
	time_t set_mtime;

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

int sieve_ldap_script_sequence_init(struct sieve_script_sequence *sseq);
int sieve_ldap_script_sequence_next(struct sieve_script_sequence *sseq,
				    struct sieve_script **script_r);
void sieve_ldap_script_sequence_destroy(struct sieve_script_sequence *sseq);

#endif

#endif
