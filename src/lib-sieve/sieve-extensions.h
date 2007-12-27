#ifndef __SIEVE_EXTENSIONS_H
#define __SIEVE_EXTENSIONS_H

#include "lib.h"
#include "sieve-common.h"

struct sieve_extension;

const struct sieve_extension *sieve_extension_acquire(const char *extension);

/* Extensions state */

bool sieve_extensions_init(const char *sieve_plugins ATTR_UNUSED); 
const int *sieve_extensions_get_preloaded_ext_ids(void);
void sieve_extensions_deinit(void);

/* Extension registry */

int sieve_extension_register(const struct sieve_extension *extension);
int sieve_extensions_get_count(void);
const struct sieve_extension *sieve_extension_get_by_id(unsigned int ext_id); 
int sieve_extension_get_by_name(const char *name, const struct sieve_extension **ext); 
int sieve_extension_get_id(const struct sieve_extension *extension); 

#endif /* __SIEVE_EXTENSIONS_H */
