/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "mempool.h"
#include "hash.h"
#include "array.h"

#include "sieve-error.h"
#include "sieve-extensions.h"

/*
 * Forward declarations 
 */

static void sieve_extensions_init_registry(void);
static void sieve_extensions_deinit_registry(void);

static void sieve_extensions_init_capabilities(void);
static void sieve_extensions_deinit_capabilities(void);

/* 
 * Pre-loaded 'extensions' 
 */

extern const struct sieve_extension comparator_extension;
extern const struct sieve_extension match_type_extension;
extern const struct sieve_extension address_part_extension;

const struct sieve_extension *sieve_preloaded_extensions[] = {
	&comparator_extension, &match_type_extension, &address_part_extension
};

const unsigned int sieve_preloaded_extensions_count = 
	N_ELEMENTS(sieve_preloaded_extensions);

/* 
 * Dummy extensions 
 */
 
/* FIXME: This is stupid. Define a comparator-* extension and be done with it */

static const struct sieve_extension comparator_i_octet_extension = {
	"comparator-i;octet", 
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_NO_OPERATIONS, 
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

static const struct sieve_extension comparator_i_ascii_casemap_extension = {
	"comparator-i;ascii-casemap", 
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 
	SIEVE_EXT_DEFINE_NO_OPERATIONS, 
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

/* 
 * Core extensions 
 */

extern const struct sieve_extension fileinto_extension;
extern const struct sieve_extension reject_extension;
extern const struct sieve_extension envelope_extension;
extern const struct sieve_extension encoded_character_extension;

/* 
 * Native 'plugin' extensions 
 */

extern const struct sieve_extension vacation_extension;
extern const struct sieve_extension subaddress_extension;
extern const struct sieve_extension comparator_i_ascii_numeric_extension;
extern const struct sieve_extension relational_extension;
extern const struct sieve_extension regex_extension;
extern const struct sieve_extension imap4flags_extension;
extern const struct sieve_extension copy_extension;
extern const struct sieve_extension include_extension;
extern const struct sieve_extension body_extension;
extern const struct sieve_extension variables_extension;
extern const struct sieve_extension enotify_extension;
extern const struct sieve_extension environment_extension;
extern const struct sieve_extension mailbox_extension;

/*
 * Extensions under development
 */

#ifdef HAVE_SIEVE_UNFINISHED

extern const struct sieve_extension ereject_extension;
extern const struct sieve_extension date_extension;

#endif

/*
 * List of native extensions
 */

const struct sieve_extension *sieve_core_extensions[] = {
	/* Preloaded 'extensions' */
	&comparator_extension, &match_type_extension, &address_part_extension,
	
	/* Dummy extensions */ 
	&comparator_i_octet_extension, &comparator_i_ascii_casemap_extension, 
	
	/* Core extensions */
	&fileinto_extension, &reject_extension, &envelope_extension, 
	&encoded_character_extension,

	/* Extensions under development */

#ifdef HAVE_SIEVE_UNFINISHED
	&ereject_extension,
	&date_extension,
#endif
	
	/* 'Plugins' */
	&vacation_extension, &subaddress_extension, 
	&comparator_i_ascii_numeric_extension, 
	&relational_extension, &regex_extension, &imap4flags_extension,
	&copy_extension, &include_extension, &body_extension,
	&variables_extension, &enotify_extension, &environment_extension,
	&mailbox_extension
};

const unsigned int sieve_core_extensions_count =
	N_ELEMENTS(sieve_core_extensions);

/*
 * Deprecated extensions
 */

extern const struct sieve_extension imapflags_extension;
extern const struct sieve_extension notify_extension;

const struct sieve_extension *sieve_deprecated_extensions[] = {
	/* Deprecated extensions */
	&imapflags_extension,
	&notify_extension
};

const unsigned int sieve_deprecated_extensions_count =
	N_ELEMENTS(sieve_deprecated_extensions);

/* 
 * Extensions init/deinit
 */

bool sieve_extensions_init(void) 
{
	unsigned int i;
	
	sieve_extensions_init_registry();
	sieve_extensions_init_capabilities();
	
	/* Pre-load core extensions */
	for ( i = 0; i < sieve_core_extensions_count; i++ ) {
		(void)sieve_extension_register(sieve_core_extensions[i], TRUE);
	}

	/* Register deprecated extensions */
	for ( i = 0; i < sieve_deprecated_extensions_count; i++ ) {
		(void)sieve_extension_register(sieve_deprecated_extensions[i], FALSE);
	}

	/* More extensions can be added through plugins */
	
	return TRUE;
}

void sieve_extensions_deinit(void)
{	
	sieve_extensions_deinit_capabilities();
	sieve_extensions_deinit_registry();
}

/* 
 * Extension registry
 */
 
struct sieve_extension_registration {
	const struct sieve_extension *extension;
	int id;
	bool required;
	bool loaded;
};

static ARRAY_DEFINE(extensions, struct sieve_extension_registration); 
static struct hash_table *extension_index; 

static void sieve_extensions_init_registry(void)
{	
	i_array_init(&extensions, 30);
	extension_index = hash_table_create
		(default_pool, default_pool, 0, str_hash, (hash_cmp_callback_t *)strcmp);
}

static bool _sieve_extension_load
(const struct sieve_extension *extension)
{
	/* Call load handler */
	if ( extension->load != NULL && !extension->load() ) {
		sieve_sys_error("failed to load '%s' extension support.", 
			extension->name);
		return FALSE;
	}

	return TRUE;
}

static struct sieve_extension_registration *_sieve_extension_register
(const struct sieve_extension *extension, bool load)
{
	struct sieve_extension_registration *ereg = 
		(struct sieve_extension_registration *)	
		hash_table_lookup(extension_index, extension->name);

	/* Register extension if it is not registered already */
	if ( ereg == NULL ) {
		int ext_id = array_count(&extensions);

		/* Add extension to the registry */

		ereg = array_append_space(&extensions);
		ereg->id = ext_id;

		hash_table_insert(extension_index, (void *) extension->name, (void *) ereg);
	}

	/* Enable extension */
	if ( extension->_id != NULL && load ) {
		/* Make sure extension is enabled */
		*(extension->_id) = ereg->id;

		/* Call load handler if extension was not loaded already */
		if ( !ereg->loaded ) {
			if ( !_sieve_extension_load(extension) )
				return NULL;
		}

		ereg->loaded = TRUE;
	}

	ereg->extension = extension;

	return ereg;
}

int sieve_extension_register
(const struct sieve_extension *extension, bool load) 
{
	struct sieve_extension_registration *ereg;

	/* Register the extension */
	if ( (ereg=_sieve_extension_register(extension, load)) == NULL ) {
		return -1;
	}

	return ereg->id;
}

int sieve_extension_require(const struct sieve_extension *extension)
{
	struct sieve_extension_registration *ereg;

	/* Register (possibly unknown) extension */
    if ( (ereg=_sieve_extension_register(extension, TRUE)) == NULL ) {
        return -1;
    }

	ereg->required = TRUE;
	return ereg->id;
}

int sieve_extensions_get_count(void)
{
	return array_count(&extensions);
}

const struct sieve_extension *sieve_extension_get_by_id(unsigned int ext_id) 
{
	const struct sieve_extension_registration *ereg;
	
	if ( ext_id < array_count(&extensions) ) {
		ereg = array_idx(&extensions, ext_id);

		if ( SIEVE_EXT_ENABLED(ereg->extension) )
			return ereg->extension;
	}
	
	return NULL;
}

const struct sieve_extension *sieve_extension_get_by_name(const char *name) 
{
	struct sieve_extension_registration *ereg;
	
	if ( *name == '@' )
		return NULL;	
		
	ereg = (struct sieve_extension_registration *) 
		hash_table_lookup(extension_index, name);

	if ( ereg == NULL || !SIEVE_EXT_ENABLED(ereg->extension) )
		return NULL;
		
	return ereg->extension;
}

static inline bool _list_extension
	(const struct sieve_extension_registration *ereg)
{
	return 
		( SIEVE_EXT_ENABLED(ereg->extension) && 
			*(ereg->extension->name) != '@' );
}

const char *sieve_extensions_get_string(void)
{
	unsigned int i, ext_count;
	const struct sieve_extension_registration *eregs;
	string_t *extstr = t_str_new(256);

	eregs = array_get(&extensions, &ext_count);

	if ( ext_count > 0 ) {
		i = 0;
		
		/* Find first listable extension */
		while ( i < ext_count && !_list_extension(&eregs[i]) )
			i++;

		if ( i < ext_count ) {
			/* Add first to string */
			str_append(extstr, eregs[i].extension->name);
			i++;	 

	 		/* Add others */
			for ( ; i < ext_count; i++ ) {
				if ( _list_extension(&eregs[i]) ) {
					str_append_c(extstr, ' ');
					str_append(extstr, eregs[i].extension->name);
				}
			}
		}
	}

	return str_c(extstr);
}

static void sieve_extension_enable(struct sieve_extension_registration *ereg)
{
	if ( ereg->extension->_id != NULL ) {
		*(ereg->extension->_id) = ereg->id;
	
		if ( !ereg->loaded ) {
			(void)_sieve_extension_load(ereg->extension);
		}
	}

	ereg->loaded = TRUE;
}

static void sieve_extension_disable(struct sieve_extension_registration *ereg)
{
	if ( ereg->extension->_id != NULL )
		*(ereg->extension->_id) = -1;	
}

void sieve_extensions_set_string(const char *ext_string)
{
	ARRAY_DEFINE(enabled_extensions, const struct sieve_extension *);
	ARRAY_DEFINE(disabled_extensions, const struct sieve_extension *);
	const struct sieve_extension *const *ext_enabled;
	const struct sieve_extension *const *ext_disabled;
	struct sieve_extension_registration *eregs;
	const char **ext_names;
	unsigned int i, ext_count, ena_count, dis_count;
	bool relative = FALSE;

	if ( ext_string == NULL ) {
		/* Enable all */
		eregs = array_get_modifiable(&extensions, &ext_count);
		
		for ( i = 0; i < ext_count; i++ )
			sieve_extension_enable(&eregs[i]);

		return;	
	}

	T_BEGIN {
		t_array_init(&enabled_extensions, array_count(&extensions));
		t_array_init(&disabled_extensions, array_count(&extensions));

		ext_names = t_strsplit_spaces(ext_string, " \t");

		while ( *ext_names != NULL ) {
			const char *name = *ext_names;

			ext_names++;

			if ( *name != '\0' ) {
				const struct sieve_extension_registration *ereg;
				char op = '\0'; /* No add/remove operation */
	
				if ( *name == '+' 		/* Add to existing config */
					|| *name == '-' ) {	/* Remove from existing config */
				 	op = *name++;
				 	relative = TRUE;
				}

				if ( *name == '@' )
					ereg = NULL;
				else
					ereg = (const struct sieve_extension_registration *) 
						hash_table_lookup(extension_index, name);
	
				if ( ereg == NULL ) {
					sieve_sys_warning(
						"ignored unknown extension '%s' while configuring "
						"available extensions", name);
					continue;
				}

				if ( op == '-' )
					array_append(&disabled_extensions, &ereg->extension, 1);
				else
					array_append(&enabled_extensions, &ereg->extension, 1);
			}
		}

		eregs = array_get_modifiable(&extensions, &ext_count);
		ext_enabled = array_get(&enabled_extensions, &ena_count);
		ext_disabled = array_get(&disabled_extensions, &dis_count);

		/* Set new extension status */

		for ( i = 0; i < ext_count; i++ ) {
			unsigned int j;
			bool disabled = TRUE;

			/* If extensions are specified relative to the default set,
			 * we first need to check which ones are disabled 
			 */

			if ( relative ) {
				/* Enable if core extension */
				for ( j = 0; j < sieve_core_extensions_count; j++ ) {
					if ( sieve_core_extensions[j] == eregs[i].extension ) {
						disabled = FALSE;
						break;
					}
    			}

				/* Disable if explicitly disabled */
				for ( j = 0; j < dis_count; j++ ) {
					if ( ext_disabled[j] == eregs[i].extension ) {
						disabled = TRUE;
						break;
					}
				}
			} 

			/* Enable if listed with '+' or no prefix */
	
			for ( j = 0; j < ena_count; j++ ) {
				if ( ext_enabled[j] == eregs[i].extension ) {
					disabled = FALSE;
					break;
				}		
			}

			/* Perform actual activation/deactivation */

			if ( eregs[i].extension->_id != NULL && 
				*(eregs[i].extension->name) != '@' ) {
				if ( disabled && !eregs[i].required )
					sieve_extension_disable(&eregs[i]);
				else
					sieve_extension_enable(&eregs[i]);
			}
		}
	} T_END;
}

static void sieve_extensions_deinit_registry(void) 
{
	struct hash_iterate_context *itx = 
		hash_table_iterate_init(extension_index);
	void *key; 
	void *value;
	
	while ( hash_table_iterate(itx, &key, &value) ) {
		struct sieve_extension_registration *ereg =
			(struct sieve_extension_registration *) value;
		const struct sieve_extension *ext = ereg->extension;
		
		if ( ext->unload != NULL )
			ext->unload();
	}

	hash_table_iterate_deinit(&itx); 	

	array_free(&extensions);
	hash_table_destroy(&extension_index);
}

/*
 * Extension capabilities
 */

static struct hash_table *capabilities_index; 

static void sieve_extensions_init_capabilities(void)
{	
	capabilities_index = hash_table_create
		(default_pool, default_pool, 0, str_hash, (hash_cmp_callback_t *)strcmp);
}

static void sieve_extensions_deinit_capabilities(void) 
{
	hash_table_destroy(&capabilities_index);
}

void sieve_extension_capabilities_register
	(const struct sieve_extension_capabilities *cap) 
{	
	hash_table_insert
		(capabilities_index, (void *) cap->name, (void *) cap);
}

const char *sieve_extension_capabilities_get_string
	(const char *cap_name) 
{
  const struct sieve_extension_capabilities *cap = 
		(const struct sieve_extension_capabilities *) 
			hash_table_lookup(capabilities_index, cap_name);

	if ( cap == NULL || cap->get_string == NULL || 
		!SIEVE_EXT_ENABLED(cap->extension) )
		return NULL;
		
	return cap->get_string();
}




