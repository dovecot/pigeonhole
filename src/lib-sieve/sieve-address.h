#ifndef __SIEVE_ADDRESS_H
#define __SIEVE_ADDRESS_H

struct sieve_address {
	const char *local_part;
	const char *domain;
};

const char *sieve_address_normalize
	(string_t *address, const char **error_r);
bool sieve_address_validate
	(string_t *address, const char **error_r);

const struct sieve_address *sieve_address_parse_envelope_path
	(const char *field_value);

#endif
