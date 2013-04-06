/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "mempool.h"
#include "hash.h"
#include "array.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-settings.h"
#include "sieve-extensions.h"

/*
 * Forward declarations
 */

static void sieve_extension_registry_init(struct sieve_instance *svinst);
static void sieve_extension_registry_deinit(struct sieve_instance *svinst);

static void sieve_capability_registry_init(struct sieve_instance *svinst);
static void sieve_capability_registry_deinit(struct sieve_instance *svinst);

static struct sieve_extension *_sieve_extension_register
	(struct sieve_instance *svinst, const struct sieve_extension_def *extdef,
		bool load, bool required);

/*
 * Instance global context
 */

struct sieve_extension_registry {
	ARRAY(struct sieve_extension *) extensions;
	HASH_TABLE(const char *, struct sieve_extension *) extension_index;
	HASH_TABLE(const char *, struct sieve_capability_registration *) capabilities_index;

	/* Core language 'extensions' */
	const struct sieve_extension *comparator_extension;
	const struct sieve_extension *match_type_extension;
	const struct sieve_extension *address_part_extension;

	/* Preloaded extensions */
	ARRAY(const struct sieve_extension *) preloaded_extensions;
};

/*
 * Pre-loaded 'extensions'
 */

extern const struct sieve_extension_def comparator_extension;
extern const struct sieve_extension_def match_type_extension;
extern const struct sieve_extension_def address_part_extension;

/*
 * Dummy extensions
 */

/* FIXME: This is stupid. Define a comparator-* extension and be done with it */

static const struct sieve_extension_def comparator_i_octet_extension = {
	.name = "comparator-i;octet",
};

static const struct sieve_extension_def comparator_i_ascii_casemap_extension = {
	.name = "comparator-i;ascii-casemap",
};

/*
 * Core extensions
 */

extern const struct sieve_extension_def fileinto_extension;
extern const struct sieve_extension_def reject_extension;
extern const struct sieve_extension_def envelope_extension;
extern const struct sieve_extension_def encoded_character_extension;

/*
 * Native 'plugin' extensions
 */

extern const struct sieve_extension_def vacation_extension;
extern const struct sieve_extension_def vacation_seconds_extension;
extern const struct sieve_extension_def subaddress_extension;
extern const struct sieve_extension_def comparator_i_ascii_numeric_extension;
extern const struct sieve_extension_def relational_extension;
extern const struct sieve_extension_def regex_extension;
extern const struct sieve_extension_def imap4flags_extension;
extern const struct sieve_extension_def copy_extension;
extern const struct sieve_extension_def include_extension;
extern const struct sieve_extension_def body_extension;
extern const struct sieve_extension_def variables_extension;
extern const struct sieve_extension_def enotify_extension;
extern const struct sieve_extension_def environment_extension;
extern const struct sieve_extension_def mailbox_extension;
extern const struct sieve_extension_def date_extension;
extern const struct sieve_extension_def spamtest_extension;
extern const struct sieve_extension_def spamtestplus_extension;
extern const struct sieve_extension_def virustest_extension;
extern const struct sieve_extension_def ihave_extension;
extern const struct sieve_extension_def editheader_extension;

/* vnd.dovecot. */
extern const struct sieve_extension_def debug_extension;
extern const struct sieve_extension_def duplicate_extension;

/*
 * List of native extensions
 */

const struct sieve_extension_def *sieve_dummy_extensions[] = {
	/* Dummy extensions */
	&comparator_i_octet_extension, &comparator_i_ascii_casemap_extension
};

const unsigned int sieve_dummy_extensions_count =
	N_ELEMENTS(sieve_dummy_extensions);

/* Core */

const struct sieve_extension_def *sieve_core_extensions[] = {
	/* Core extensions */
	&fileinto_extension, &reject_extension, &envelope_extension,
	&encoded_character_extension,

	/* 'Plugins' */
	&vacation_extension, &subaddress_extension,
	&comparator_i_ascii_numeric_extension,
	&relational_extension, &regex_extension, &imap4flags_extension,
	&copy_extension, &include_extension, &body_extension,
	&variables_extension, &enotify_extension, &environment_extension,
	&mailbox_extension, &date_extension, &ihave_extension,
};

const unsigned int sieve_core_extensions_count =
	N_ELEMENTS(sieve_core_extensions);

/* Extra;
 *   These are not enabled by default, e.g. because explicit configuration is
 *   necessary to make these useful.
 */

const struct sieve_extension_def *sieve_extra_extensions[] = {
	&vacation_seconds_extension, &spamtest_extension, &spamtestplus_extension,
	&virustest_extension, &editheader_extension,

	/* vnd.dovecot. */
	&debug_extension, &duplicate_extension
};

const unsigned int sieve_extra_extensions_count =
	N_ELEMENTS(sieve_extra_extensions);

/*
 * Deprecated extensions
 */

extern const struct sieve_extension_def imapflags_extension;
extern const struct sieve_extension_def notify_extension;

const struct sieve_extension_def *sieve_deprecated_extensions[] = {
	&imapflags_extension,
	&notify_extension
};

const unsigned int sieve_deprecated_extensions_count =
	N_ELEMENTS(sieve_deprecated_extensions);

/*
 * Unfinished extensions
 */

#ifdef HAVE_SIEVE_UNFINISHED

extern const struct sieve_extension_def ereject_extension;

const struct sieve_extension_def *sieve_unfinished_extensions[] = {
	&ereject_extension
};

const unsigned int sieve_unfinished_extensions_count =
	N_ELEMENTS(sieve_unfinished_extensions);

#endif /* HAVE_SIEVE_UNFINISHED */

/*
 * Extensions init/deinit
 */

bool sieve_extensions_init(struct sieve_instance *svinst)
{
	unsigned int i;
	struct sieve_extension_registry *ext_reg =
		p_new(svinst->pool, struct sieve_extension_registry, 1);
	struct sieve_extension *ext;

	svinst->ext_reg = ext_reg;

	sieve_extension_registry_init(svinst);
	sieve_capability_registry_init(svinst);

	/* Preloaded 'extensions' */
	ext_reg->comparator_extension =
		sieve_extension_register(svinst, &comparator_extension, TRUE);
	ext_reg->match_type_extension =
		sieve_extension_register(svinst, &match_type_extension, TRUE);
	ext_reg->address_part_extension =
		sieve_extension_register(svinst, &address_part_extension, TRUE);

	p_array_init(&ext_reg->preloaded_extensions, svinst->pool, 5);
	array_append(&ext_reg->preloaded_extensions,
		&ext_reg->comparator_extension, 1);
	array_append(&ext_reg->preloaded_extensions,
		&ext_reg->match_type_extension, 1);
	array_append(&ext_reg->preloaded_extensions,
		&ext_reg->address_part_extension, 1);

	/* Pre-load dummy extensions */
	for ( i = 0; i < sieve_dummy_extensions_count; i++ ) {
		if ( (ext=_sieve_extension_register
			(svinst, sieve_dummy_extensions[i], TRUE, FALSE)) == NULL )
			return FALSE;

		ext->dummy = TRUE;
	}

	/* Pre-load core extensions */
	for ( i = 0; i < sieve_core_extensions_count; i++ ) {
		if ( sieve_extension_register
			(svinst, sieve_core_extensions[i], TRUE) == NULL )
			return FALSE;
	}

	/* Pre-load extra extensions */
	for ( i = 0; i < sieve_extra_extensions_count; i++ ) {
		if ( sieve_extension_register
			(svinst, sieve_extra_extensions[i], FALSE) == NULL )
			return FALSE;
	}

	/* Register deprecated extensions */
	for ( i = 0; i < sieve_deprecated_extensions_count; i++ ) {
		if ( sieve_extension_register
			(svinst, sieve_deprecated_extensions[i], FALSE) == NULL )
			return FALSE;
	}

#ifdef HAVE_SIEVE_UNFINISHED
	/* Register unfinished extensions */
	for ( i = 0; i < sieve_unfinished_extensions_count; i++ ) {
		if ( sieve_extension_register
			(svinst, sieve_unfinished_extensions[i], FALSE) == NULL )
			return FALSE;
	}
#endif

	/* More extensions can be added through plugins */

	return TRUE;
}

void sieve_extensions_configure(struct sieve_instance *svinst)
{
	const char *extensions;

	/* Apply sieve_extensions configuration */

	if ( (extensions=sieve_setting_get(svinst, "sieve_extensions")) != NULL )
		sieve_extensions_set_string(svinst, extensions, FALSE);

	/* Apply sieve_global_extensions configuration */

	if ( (extensions=sieve_setting_get(svinst, "sieve_global_extensions")) != NULL )
		sieve_extensions_set_string(svinst, extensions, TRUE);
}

void sieve_extensions_deinit(struct sieve_instance *svinst)
{
	sieve_extension_registry_deinit(svinst);
	sieve_capability_registry_deinit(svinst);
}

/*
 * Pre-loaded extensions
 */

const struct sieve_extension *const *sieve_extensions_get_preloaded
(struct sieve_instance *svinst, unsigned int *count_r)
{
	struct sieve_extension_registry *ext_reg = svinst->ext_reg;

	return array_get(&ext_reg->preloaded_extensions, count_r);
}

/*
 * Extension registry
 */

static bool _sieve_extension_load(struct sieve_extension *ext)
{
	/* Call load handler */
	if ( ext->def != NULL && ext->def->load != NULL &&
		!ext->def->load(ext, &ext->context) ) {
		sieve_sys_error(ext->svinst,
			"failed to load '%s' extension support.", ext->def->name);
		return FALSE;
	}

	return TRUE;
}

static void _sieve_extension_unload(struct sieve_extension *ext)
{
	/* Call unload handler */
	if ( ext->def != NULL && ext->def->unload != NULL )
		ext->def->unload(ext);
}

static void sieve_extension_registry_init(struct sieve_instance *svinst)
{
	struct sieve_extension_registry *ext_reg = svinst->ext_reg;

	p_array_init(&ext_reg->extensions, svinst->pool, 50);
	hash_table_create
		(&ext_reg->extension_index, default_pool, 0, str_hash, strcmp);
}

static void sieve_extension_registry_deinit(struct sieve_instance *svinst)
{
	struct sieve_extension_registry *ext_reg = svinst->ext_reg;
	struct sieve_extension * const *exts;
    unsigned int i, ext_count;

	if ( !hash_table_is_created(ext_reg->extension_index) ) return;

    exts = array_get_modifiable(&ext_reg->extensions, &ext_count);
	for ( i = 0; i < ext_count; i++ ) {
		_sieve_extension_unload(exts[i]);
	}

	hash_table_destroy(&ext_reg->extension_index);
}

bool sieve_extension_reload(const struct sieve_extension *ext)
{
	struct sieve_extension_registry *ext_reg = ext->svinst->ext_reg;
	struct sieve_extension * const *mod_ext;
	int ext_id = ext->id;

	/* Let's not just cast the 'const' away */
	if ( ext_id >= 0 && ext_id < (int) array_count(&ext_reg->extensions) ) {
		mod_ext = array_idx(&ext_reg->extensions, ext_id);

		return _sieve_extension_load(*mod_ext);
	}

	return FALSE;
}

static struct sieve_extension *_sieve_extension_register
(struct sieve_instance *svinst, const struct sieve_extension_def *extdef,
	bool load, bool required)
{
	struct sieve_extension_registry *ext_reg = svinst->ext_reg;
	struct sieve_extension *ext =
		hash_table_lookup(ext_reg->extension_index, extdef->name);

	/* Register extension if it is not registered already */
	if ( ext == NULL ) {
		struct sieve_extension **extr;

		int ext_id = (int)array_count(&ext_reg->extensions);

		/* Add extension to the registry */

		extr = array_append_space(&ext_reg->extensions);
		*extr = ext = p_new(svinst->pool, struct sieve_extension, 1);
		ext->id = ext_id;
		ext->def = extdef;
		ext->svinst = svinst;

		hash_table_insert(ext_reg->extension_index, extdef->name, ext);

	/* Re-register it if it were previously unregistered
	 * (not going to happen)
	 */
	} else if ( ext->def == NULL ) {
		ext->def = extdef;
	}

	/* Enable extension */
	if ( load || required ) {
		ext->enabled = ( ext->enabled || load );

		/* Call load handler if extension was not loaded already */
		if ( !ext->loaded ) {
			if ( !_sieve_extension_load(ext) )
				return NULL;
		}

		ext->loaded = TRUE;
	}

	ext->required = ( ext->required || required );

	return ext;
}

const struct sieve_extension *sieve_extension_register
(struct sieve_instance *svinst, const struct sieve_extension_def *extdef,
	bool load)
{
	return _sieve_extension_register(svinst, extdef, load, FALSE);
}

void sieve_extension_unregister(const struct sieve_extension *ext)
{
	struct sieve_extension_registry *ext_reg = ext->svinst->ext_reg;
	struct sieve_extension * const *mod_ext;
	int ext_id = ext->id;

	if ( ext_id >= 0 && ext_id < (int) array_count(&ext_reg->extensions) ) {
		mod_ext = array_idx(&ext_reg->extensions, ext_id);

	sieve_extension_capabilities_unregister(*mod_ext);
	_sieve_extension_unload(*mod_ext);
	(*mod_ext)->loaded = FALSE;
	(*mod_ext)->enabled = FALSE;
	(*mod_ext)->def = NULL;
	}
}

const struct sieve_extension *sieve_extension_require
(struct sieve_instance *svinst, const struct sieve_extension_def *extdef,
	bool load)
{
	return _sieve_extension_register(svinst, extdef, load, TRUE);
}

int sieve_extensions_get_count(struct sieve_instance *svinst)
{
	struct sieve_extension_registry *ext_reg = svinst->ext_reg;

	return array_count(&ext_reg->extensions);
}

const struct sieve_extension *sieve_extension_get_by_id
(struct sieve_instance *svinst, unsigned int ext_id)
{
	struct sieve_extension_registry *ext_reg = svinst->ext_reg;
	struct sieve_extension * const *ext;

	if ( ext_id < array_count(&ext_reg->extensions) ) {
		ext = array_idx(&ext_reg->extensions, ext_id);

		if ( (*ext)->def != NULL && ((*ext)->enabled || (*ext)->required) )
			return *ext;
	}

	return NULL;
}

const struct sieve_extension *sieve_extension_get_by_name
(struct sieve_instance *svinst, const char *name)
{
	struct sieve_extension_registry *ext_reg = svinst->ext_reg;
	const struct sieve_extension *ext;

	if ( *name == '@' )
		return NULL;

	if ( strlen(name) > 128 )
		return NULL;

	ext = hash_table_lookup(ext_reg->extension_index, name);

	if ( ext == NULL || ext->def == NULL || (!ext->enabled && !ext->required))
		return NULL;

	return ext;
}

static inline bool _sieve_extension_listable(const struct sieve_extension *ext)
{
	return ( ext->enabled && ext->def != NULL && *(ext->def->name) != '@'
		&& !ext->dummy && !ext->global );
}

const char *sieve_extensions_get_string(struct sieve_instance *svinst)
{
	struct sieve_extension_registry *ext_reg = svinst->ext_reg;
	string_t *extstr = t_str_new(256);
	struct sieve_extension * const *exts;
	unsigned int i, ext_count;

	exts = array_get(&ext_reg->extensions, &ext_count);

	if ( ext_count > 0 ) {
		i = 0;

		/* Find first listable extension */
		while ( i < ext_count && !_sieve_extension_listable(exts[i]) )
			i++;

		if ( i < ext_count ) {
			/* Add first to string */
			str_append(extstr, exts[i]->def->name);
			i++;

	 		/* Add others */
			for ( ; i < ext_count; i++ ) {
				if ( _sieve_extension_listable(exts[i]) ) {
					str_append_c(extstr, ' ');
					str_append(extstr, exts[i]->def->name);
				}
			}
		}
	}

	return str_c(extstr);
}

static void sieve_extension_set_enabled
(struct sieve_extension *ext, bool enabled)
{
	if ( enabled ) {
		ext->enabled = TRUE;
		ext->global = FALSE;

		if ( !ext->loaded ) {
			(void)_sieve_extension_load(ext);
		}

		ext->loaded = TRUE;
	} else {
		ext->enabled = FALSE;
	}
}

static void sieve_extension_set_global
(struct sieve_extension *ext, bool enabled)
{
	if ( enabled ) {
		ext->enabled = TRUE;
		ext->global = TRUE;

		if ( !ext->loaded ) {
			(void)_sieve_extension_load(ext);
		}

		ext->loaded = TRUE;
	} else {
		ext->global = FALSE;
	}
}

void sieve_extensions_set_string
(struct sieve_instance *svinst, const char *ext_string, bool global)
{
	struct sieve_extension_registry *ext_reg = svinst->ext_reg;
	ARRAY(const struct sieve_extension *) enabled_extensions;
	ARRAY(const struct sieve_extension *) disabled_extensions;
	const struct sieve_extension *const *ext_enabled;
	const struct sieve_extension *const *ext_disabled;
	struct sieve_extension **exts;
	const char **ext_names;
	unsigned int i, ext_count, ena_count, dis_count;
	bool relative = FALSE;

	if ( ext_string == NULL ) {
		if ( global ) return;

		/* Enable all */
		exts = array_get_modifiable(&ext_reg->extensions, &ext_count);

		for ( i = 0; i < ext_count; i++ )
			sieve_extension_set_enabled(exts[i], TRUE);

		return;
	}

	T_BEGIN {
		t_array_init(&enabled_extensions, array_count(&ext_reg->extensions));
		t_array_init(&disabled_extensions, array_count(&ext_reg->extensions));

		ext_names = t_strsplit_spaces(ext_string, " \t");

		while ( *ext_names != NULL ) {
			const char *name = *ext_names;

			ext_names++;

			if ( *name != '\0' ) {
				const struct sieve_extension *ext;
				char op = '\0'; /* No add/remove operation */

				if ( *name == '+' 		/* Add to existing config */
					|| *name == '-' ) {	/* Remove from existing config */
				 	op = *name++;
				 	relative = TRUE;
				}

				if ( *name == '@' )
					ext = NULL;
				else
					ext = hash_table_lookup(ext_reg->extension_index, name);

				if ( ext == NULL || ext->def == NULL ) {
					sieve_sys_warning(svinst,
						"ignored unknown extension '%s' while configuring "
						"available extensions", name);
					continue;
				}

				if ( op == '-' )
					array_append(&disabled_extensions, &ext, 1);
				else
					array_append(&enabled_extensions, &ext, 1);
			}
		}

		exts = array_get_modifiable(&ext_reg->extensions, &ext_count);
		ext_enabled = array_get(&enabled_extensions, &ena_count);
		ext_disabled = array_get(&disabled_extensions, &dis_count);

		/* Set new extension status */

		for ( i = 0; i < ext_count; i++ ) {
			unsigned int j;
			bool enabled = FALSE;

			if ( exts[i]->id < 0 || exts[i]->def == NULL ||
				*(exts[i]->def->name) == '@' ) {
				continue;
			}

			/* If extensions are specified relative to the default set,
			 * we first need to check which ones are disabled
			 */

			if ( relative ) {
				if ( global )
					enabled = exts[i]->global;
				else
					enabled = exts[i]->enabled;

				if ( enabled ) {
					/* Disable if explicitly disabled */
					for ( j = 0; j < dis_count; j++ ) {
						if ( ext_disabled[j]->def == exts[i]->def ) {
							enabled = FALSE;
							break;
						}
					}
				}
			}

			/* Enable if listed with '+' or no prefix */

			for ( j = 0; j < ena_count; j++ ) {
				if ( ext_enabled[j]->def == exts[i]->def ) {
					enabled = TRUE;
					break;
				}
			}

			/* Perform actual activation/deactivation */
			if ( global ) {
				sieve_extension_set_global(exts[i], enabled);
			} else {
				sieve_extension_set_enabled(exts[i], enabled);
			}
		}
	} T_END;
}

const struct sieve_extension *sieve_get_match_type_extension
	(struct sieve_instance *svinst)
{
	return svinst->ext_reg->match_type_extension;
}

const struct sieve_extension *sieve_get_comparator_extension
	(struct sieve_instance *svinst)
{
	return svinst->ext_reg->comparator_extension;
}

const struct sieve_extension *sieve_get_address_part_extension
	(struct sieve_instance *svinst)
{
	return svinst->ext_reg->address_part_extension;
}

void sieve_enable_debug_extension(struct sieve_instance *svinst)
{
	(void) sieve_extension_register(svinst, &debug_extension, TRUE);
}

/*
 * Extension capabilities
 */

struct sieve_capability_registration {
	const struct sieve_extension *ext;
	const struct sieve_extension_capabilities *capabilities;
};

void sieve_capability_registry_init(struct sieve_instance *svinst)
{
	struct sieve_extension_registry *ext_reg = svinst->ext_reg;

	hash_table_create
		(&ext_reg->capabilities_index, default_pool, 0, str_hash, strcmp);
}

void sieve_capability_registry_deinit(struct sieve_instance *svinst)
{
	struct sieve_extension_registry *ext_reg = svinst->ext_reg;

	if ( !hash_table_is_created(ext_reg->capabilities_index) ) return;

	hash_table_destroy(&svinst->ext_reg->capabilities_index);
}

void sieve_extension_capabilities_register
(const struct sieve_extension *ext,
	const struct sieve_extension_capabilities *cap)
{
	struct sieve_instance *svinst = ext->svinst;
	struct sieve_extension_registry *ext_reg = svinst->ext_reg;
	struct sieve_capability_registration *reg =
		p_new(svinst->pool, struct sieve_capability_registration, 1);

	reg->ext = ext;
	reg->capabilities = cap;

	hash_table_insert(ext_reg->capabilities_index, cap->name, reg);
}

void sieve_extension_capabilities_unregister
(const struct sieve_extension *ext)
{
	struct sieve_extension_registry *ext_reg = ext->svinst->ext_reg;
	struct hash_iterate_context *hictx;
	const char *name;
	struct sieve_capability_registration *reg;

	hictx = hash_table_iterate_init(ext_reg->capabilities_index);
	while ( hash_table_iterate(hictx, ext_reg->capabilities_index, &name, &reg) ) {
		if ( reg->ext == ext )
			hash_table_remove(ext_reg->capabilities_index, name);
	}
	hash_table_iterate_deinit(&hictx);
}

const char *sieve_extension_capabilities_get_string
(struct sieve_instance *svinst, const char *cap_name)
{
	struct sieve_extension_registry *ext_reg = svinst->ext_reg;
	const struct sieve_capability_registration *cap_reg =
		hash_table_lookup(ext_reg->capabilities_index, cap_name);
	const struct sieve_extension_capabilities *cap;

	if ( cap_reg == NULL || cap_reg->capabilities == NULL )
		return NULL;

	cap = cap_reg->capabilities;

	if ( cap->get_string == NULL || !cap_reg->ext->enabled )
		return NULL;

	return cap->get_string(cap_reg->ext);
}




