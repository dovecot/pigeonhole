/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "time-util.h"
#include "istream.h"

#include "sieve-ldap-storage.h"

#if defined(SIEVE_BUILTIN_LDAP) || defined(PLUGIN_BUILD)

#include "str.h"
#include "strfuncs.h"

#include "sieve-error.h"
#include "sieve-dump.h"
#include "sieve-binary.h"

/*
 * Script file implementation
 */

static struct sieve_ldap_script *sieve_ldap_script_alloc(void)
{
	struct sieve_ldap_script *lscript;
	pool_t pool;

	pool = pool_alloconly_create("sieve_ldap_script", 1024);
	lscript = p_new(pool, struct sieve_ldap_script, 1);
	lscript->script = sieve_ldap_script;
	lscript->script.pool = pool;

	return lscript;
}

struct sieve_ldap_script *
sieve_ldap_script_init(struct sieve_ldap_storage *lstorage, const char *name)
{
	struct sieve_storage *storage = &lstorage->storage;
	struct sieve_ldap_script *lscript = NULL;
	const char *location;

	if (name == NULL) {
		name = SIEVE_LDAP_SCRIPT_DEFAULT;
		location = storage->location;
	} else {
		location = t_strconcat(storage->location, ";name=", name, NULL);
	}

	lscript = sieve_ldap_script_alloc();
	sieve_script_init(&lscript->script, storage, &sieve_ldap_script,
			  location, name);
	return lscript;
}

static int
sieve_ldap_script_open(struct sieve_script *script)
{
	struct sieve_ldap_script *lscript =
		container_of(script, struct sieve_ldap_script, script);
	struct sieve_storage *storage = script->storage;
	struct sieve_ldap_storage *lstorage =
		container_of(storage, struct sieve_ldap_storage, storage);
	int ret;

	if (sieve_ldap_db_connect(lstorage->conn) < 0) {
		sieve_storage_set_critical(
			storage, "Failed to connect to LDAP database");
		return -1;
	}

	ret = sieve_ldap_db_lookup_script(lstorage->conn, script->name,
					  &lscript->dn, &lscript->modattr);
	if (ret <= 0) {
		if (ret == 0) {
			e_debug(script->event, "Script entry not found");
			sieve_script_set_not_found_error(script, NULL);
		} else {
			sieve_script_set_internal_error(script);
		}
		return -1;
	}

	return 0;
}

static int
sieve_ldap_script_get_stream(struct sieve_script *script,
			     struct istream **stream_r)
{
	struct sieve_ldap_script *lscript =
		container_of(script, struct sieve_ldap_script, script);
	struct sieve_storage *storage = script->storage;
	struct sieve_ldap_storage *lstorage =
		container_of(storage, struct sieve_ldap_storage, storage);
	int ret;

	i_assert(lscript->dn != NULL);

	ret = sieve_ldap_db_read_script(lstorage->conn, lscript->dn, stream_r);
	if (ret <= 0) {
		if (ret == 0) {
			e_debug(script->event, "Script attribute not found");
			sieve_script_set_not_found_error(script, NULL);
		} else {
			sieve_script_set_internal_error(script);
		}
		return -1;
	}
	return 0;
}

static int
sieve_ldap_script_binary_read_metadata(struct sieve_script *script,
				       struct sieve_binary_block *sblock,
				       sieve_size_t *offset)
{
	struct sieve_ldap_script *lscript =
		container_of(script, struct sieve_ldap_script, script);
	struct sieve_storage *storage = script->storage;
	struct sieve_instance *svinst = storage->svinst;
	struct sieve_ldap_storage *lstorage =
		container_of(storage, struct sieve_ldap_storage, storage);
	struct sieve_binary *sbin = sieve_binary_block_get_binary(sblock);
	time_t bmtime = sieve_binary_mtime(sbin);
	string_t *dn, *modattr;

	/* Config file changed? */
	if (bmtime <= lstorage->set_mtime) {
		if (svinst->debug) {
			e_debug(script->event,
				"Sieve binary '%s' is not newer "
				"than the LDAP configuration '%s' (%s <= %s)",
				sieve_binary_path(sbin), lstorage->config_file,
				t_strflocaltime("%Y-%m-%d %H:%M:%S", bmtime),
				t_strflocaltime("%Y-%m-%d %H:%M:%S",
						lstorage->set_mtime));
		}
		return 0;
	}

	/* Open script if not open already */
	if (lscript->dn == NULL && sieve_script_open(script, NULL) < 0)
		return 0;

	/* If modattr not found, recompile always */
	if (lscript->modattr == NULL || *lscript->modattr == '\0') {
		e_error(script->event,
			"LDAP entry for script '%s' "
			"has no modified attribute '%s'",
			sieve_script_location(script),
			lstorage->set->sieve_ldap_mod_attr);
		return 0;
	}

	/* Compare DN in binary and from search result */
	if (!sieve_binary_read_string(sblock, offset, &dn)) {
		e_error(script->event,
			"Binary '%s' has invalid metadata for script '%s': "
			"Invalid DN",
			sieve_binary_path(sbin), sieve_script_location(script));
		return -1;
	}
	i_assert(lscript->dn != NULL);
	if (strcmp(str_c(dn), lscript->dn) != 0) {
		e_debug(script->event,
			"Binary '%s' reports different LDAP DN for script '%s' "
			"('%s' rather than '%s')",
			sieve_binary_path(sbin), sieve_script_location(script),
			str_c(dn), lscript->dn);
		return 0;
	}

	/* Compare modattr in binary and from search result */
	if (!sieve_binary_read_string(sblock, offset, &modattr)) {
		e_error(script->event,
			"Binary '%s' has invalid metadata for script '%s': "
			"Invalid modified attribute",
			sieve_binary_path(sbin), sieve_script_location(script));
		return -1;
	}
	if (strcmp(str_c(modattr), lscript->modattr) != 0) {
		e_debug(script->event,
			"Binary '%s' reports different modified attribute content "
			"for script '%s' ('%s' rather than '%s')",
			sieve_binary_path(sbin), sieve_script_location(script),
			str_c(modattr), lscript->modattr);
		return 0;
	}
	return 1;
}

static void
sieve_ldap_script_binary_write_metadata(struct sieve_script *script,
					struct sieve_binary_block *sblock)
{
	struct sieve_ldap_script *lscript =
		container_of(script, struct sieve_ldap_script, script);

	sieve_binary_emit_cstring(sblock, lscript->dn);
	if (lscript->modattr == NULL)
		sieve_binary_emit_cstring(sblock, "");
	else
		sieve_binary_emit_cstring(sblock, lscript->modattr);
}

static bool
sieve_ldap_script_binary_dump_metadata(struct sieve_script *script ATTR_UNUSED,
				       struct sieve_dumptime_env *denv,
				       struct sieve_binary_block *sblock,
				       sieve_size_t *offset)
{
	string_t *dn, *modattr;

	if (!sieve_binary_read_string(sblock, offset, &dn))
		return FALSE;
	sieve_binary_dumpf(denv, "ldap.dn = %s\n", str_c(dn));

	if (!sieve_binary_read_string(sblock, offset, &modattr))
		return FALSE;
	sieve_binary_dumpf(denv, "ldap.mod_attr = %s\n", str_c(modattr));

	return TRUE;
}

static const char *
sieve_ldap_script_get_bin_path(struct sieve_ldap_script *lscript)
{
	struct sieve_script *script = &lscript->script;
	struct sieve_storage *storage = script->storage;

	if (lscript->bin_path == NULL) {
		if (storage->bin_path == NULL)
			return NULL;
		lscript->bin_path = p_strconcat(
			script->pool, storage->bin_path, "/",
			sieve_binfile_from_name(script->name), NULL);
	}
	return lscript->bin_path;
}

static int
sieve_ldap_script_binary_load(struct sieve_script *script,
			      struct sieve_binary **sbin_r)
{
	struct sieve_ldap_script *lscript =
		container_of(script, struct sieve_ldap_script, script);

	return sieve_script_binary_load_default(
		script, sieve_ldap_script_get_bin_path(lscript), sbin_r);
}

static int
sieve_ldap_script_binary_save(struct sieve_script *script,
			      struct sieve_binary *sbin, bool update)
{
	struct sieve_ldap_script *lscript =
		container_of(script, struct sieve_ldap_script, script);

	return sieve_script_binary_save_default(
		script, sbin, sieve_ldap_script_get_bin_path(lscript),
		update, 0600);
}

const struct sieve_script sieve_ldap_script = {
	.driver_name = SIEVE_LDAP_STORAGE_DRIVER_NAME,
	.v = {
		.open = sieve_ldap_script_open,

		.get_stream = sieve_ldap_script_get_stream,

		.binary_read_metadata = sieve_ldap_script_binary_read_metadata,
		.binary_write_metadata =
			sieve_ldap_script_binary_write_metadata,
		.binary_dump_metadata = sieve_ldap_script_binary_dump_metadata,
		.binary_load = sieve_ldap_script_binary_load,
		.binary_save = sieve_ldap_script_binary_save,
	},
};

/*
 * Script sequence
 */

struct sieve_ldap_script_sequence {
	bool done:1;
};

int sieve_ldap_script_sequence_init(struct sieve_script_sequence *sseq)
{
	struct sieve_ldap_script_sequence *lseq = NULL;

	/* Create sequence object */
	lseq = i_new(struct sieve_ldap_script_sequence, 1);
	sseq->storage_data = lseq;

	return 0;
}

int sieve_ldap_script_sequence_next(struct sieve_script_sequence *sseq,
				    struct sieve_script **script_r)
{
	struct sieve_ldap_script_sequence *lseq = sseq->storage_data;
	struct sieve_storage *storage = sseq->storage;
	struct sieve_ldap_storage *lstorage =
		container_of(storage, struct sieve_ldap_storage, storage);
	struct sieve_ldap_script *lscript;

	if (lseq->done)
		return 0;
	lseq->done = TRUE;

	lscript = sieve_ldap_script_init(lstorage, storage->script_name);
	if (sieve_script_open(&lscript->script, NULL) < 0) {
		struct sieve_script *script = &lscript->script;

		sieve_script_unref(&script);
		return -1;
	}
	*script_r = &lscript->script;
	return 1;
}

void sieve_ldap_script_sequence_destroy(struct sieve_script_sequence *sseq)
{
	struct sieve_ldap_script_sequence *lseq = sseq->storage_data;

	i_free(lseq);
}

#endif
