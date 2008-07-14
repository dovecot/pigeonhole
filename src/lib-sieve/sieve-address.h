#ifndef __SIEVE_ADDRESS_H
#define __SIEVE_ADDRESS_H

const char *sieve_address_normalize
	(string_t *address, const char **error_r);
bool sieve_address_validate
	(string_t *address, const char **error_r);

#endif
