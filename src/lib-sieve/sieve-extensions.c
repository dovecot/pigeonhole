#include "lib.h"
#include "str.h"
#include "mempool.h"
#include "hash.h"
#include "array.h"

#include "sieve-error.h"
#include "sieve-extensions-private.h"

/* Static pre-declarations */

static void sieve_extensions_init_registry(void);
static void sieve_extensions_deinit_registry(void);

/* Pre-loaded extensions */

extern const struct sieve_extension comparator_extension;
extern const struct sieve_extension match_type_extension;
extern const struct sieve_extension address_part_extension;

const struct sieve_extension *sieve_preloaded_extensions[] = {
	&comparator_extension, &match_type_extension, &address_part_extension
};

const unsigned int sieve_preloaded_extensions_count = 
	N_ELEMENTS(sieve_preloaded_extensions);

/* Dummy extensions */

static const struct sieve_extension comparator_i_octet_extension = {
	"comparator-i;octet", 
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_NO_OPERATIONS, 
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

static const struct sieve_extension comparator_i_ascii_casemap_extension = {
	"comparator-i;ascii-casemap", 
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_NO_OPERATIONS, 
	SIEVE_EXT_DEFINE_NO_OPERANDS
};

/* Base extensions */

extern const struct sieve_extension fileinto_extension;
extern const struct sieve_extension reject_extension;
extern const struct sieve_extension envelope_extension;
extern const struct sieve_extension encoded_character_extension;

/* Plugins (FIXME: make this dynamic) */

extern const struct sieve_extension vacation_extension;
extern const struct sieve_extension subaddress_extension;
extern const struct sieve_extension comparator_i_ascii_numeric_extension;
extern const struct sieve_extension relational_extension;
extern const struct sieve_extension regex_extension;
extern const struct sieve_extension imapflags_extension;
extern const struct sieve_extension copy_extension;
extern const struct sieve_extension include_extension;
extern const struct sieve_extension body_extension;
extern const struct sieve_extension variables_extension;

const struct sieve_extension *sieve_core_extensions[] = {
	/* Preloaded 'extensions' */
	&comparator_extension, &match_type_extension, &address_part_extension,
	
	/* Dummy extensions */ 
	&comparator_i_octet_extension, &comparator_i_ascii_casemap_extension, 
	
	/* Base extensions */
	&fileinto_extension, &reject_extension, &envelope_extension, 
	&encoded_character_extension,
	
	/* 'Plugins' */
	&vacation_extension, &subaddress_extension, 
	&comparator_i_ascii_numeric_extension, 
	&relational_extension, &regex_extension, &imapflags_extension,
	&copy_extension, &include_extension, &body_extension,
	&variables_extension
};

const unsigned int sieve_core_extensions_count =
	N_ELEMENTS(sieve_core_extensions);

/* Extension state */

bool sieve_extensions_init(const char *sieve_plugins ATTR_UNUSED) 
{
	unsigned int i;
	
	sieve_extensions_init_registry();
	
	/* Pre-load core extensions */
	for ( i = 0; i < sieve_core_extensions_count; i++ ) {
		(void) sieve_extension_register(sieve_core_extensions[i]);
	}
	
	/* More extensions can be added through plugins */
	
	return TRUE;
}

void sieve_extensions_deinit(void)
{	
	sieve_extensions_deinit_registry();
}

/* Extension registry */

struct sieve_extension_registration {
	const struct sieve_extension *extension;
	int id;
};

static ARRAY_DEFINE(extensions, const struct sieve_extension *); 
static struct hash_table *extension_index; 

static void sieve_extensions_init_registry(void)
{	
	p_array_init(&extensions, default_pool, 4);
	extension_index = hash_create
		(default_pool, default_pool, 0, str_hash, (hash_cmp_callback_t *)strcmp);
}

int sieve_extension_register(const struct sieve_extension *extension) 
{
	int ext_id = array_count(&extensions); 
	struct sieve_extension_registration *ereg;
	
	array_append(&extensions, &extension, 1);
	
	ereg = p_new(default_pool, struct sieve_extension_registration, 1);
	ereg->extension = extension;
	ereg->id = ext_id;
	
	hash_insert(extension_index, (void *) extension->name, (void *) ereg);

	if ( extension->load != NULL && !extension->load(ext_id) ) {
		sieve_sys_error("failed to load '%s' extension support.", extension->name);
		return -1;
	}

	return ext_id;
}

int sieve_extensions_get_count(void)
{
	return array_count(&extensions);
}

const struct sieve_extension *sieve_extension_get_by_id(unsigned int ext_id) 
{
	const struct sieve_extension * const *ext;
	
	if ( ext_id < array_count(&extensions) ) {
		ext = array_idx(&extensions, ext_id);
		return *ext;
	}
	
	return NULL;
}

const struct sieve_extension *sieve_extension_get_by_name(const char *name) 
{
  struct sieve_extension_registration *ereg;
	
	if ( *name == '@' )
		return NULL;	
		
	ereg = (struct sieve_extension_registration *) 
		hash_lookup(extension_index, name);

	if ( ereg == NULL )
		return NULL;
		
	return ereg->extension;
}

static bool _list_extension(const struct sieve_extension *ext)
{
	return ( ext->id != NULL && *ext->name != '@' );
}

const char *sieve_extensions_get_string(void)
{
	unsigned int i = 0, ext_count = array_count(&extensions);
	string_t *extstr = t_str_new(256);

	if ( ext_count > 0 ) {
		const struct sieve_extension * const *ext =
			array_idx(&extensions, i);

		while ( !_list_extension(*ext) ) {
			if ( i < ext_count ) 
				ext = array_idx(&extensions, i);
			else
				break;
			i++;
		}

		str_append(extstr, (*ext)->name);
 
		while ( i < ext_count ) {
			ext = array_idx(&extensions, i);

			if ( _list_extension(*ext) ) {
				str_append_c(extstr, ' ');
				str_append(extstr, (*ext)->name);
			}
			i++;
		}
	}

	return str_c(extstr);
}

static void sieve_extensions_deinit_registry(void) 
{
	struct hash_iterate_context *itx = 
		hash_iterate_init(extension_index);
	void *key; 
	void *ereg;
	
	while ( hash_iterate(itx, &key, &ereg) ) {
		p_free(default_pool, ereg);
	}

	hash_iterate_deinit(&itx); 	

	array_free(&extensions);
	hash_destroy(&extension_index);
}
