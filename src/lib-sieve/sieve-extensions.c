#include "lib.h"
#include "hash.h"

#include "sieve-extensions.h"

const struct sieve_extension sieve_core_extensions[] = {
	{ "fileinto", ext_fileinto_validator_load, NULL },
	{ "reject", ext_reject_validator_load, ext_reject_generator_load },
	{ "envelope", ext_envelope_validator_load, NULL }
};

const unsigned int sieve_core_extensions_count =
	(sizeof(sieve_core_extensions) / sizeof(sieve_core_extensions[0]));

const struct sieve_extension *sieve_extension_acquire(const char *extension) {
	unsigned int i;
	
	/* First try to acquire one of the compiled-in extensions */
	for ( i = 0; i < sieve_core_extensions_count; i++ ) {
		if ( strcasecmp(extension, sieve_core_extensions[i].name) == 0 ) 
			return &sieve_core_extensions[i];
	}
	
	/* Try to load a plugin */
	
	// FIXME
	
	/* Not found */
	return NULL;
}
