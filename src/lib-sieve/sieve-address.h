/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file 
 */
 
#ifndef __SIEVE_ADDRESS_H
#define __SIEVE_ADDRESS_H
 
#include "lib.h"
#include "strfuncs.h"

/*
 * Generic address representation
 */ 
 
struct sieve_address {
	const char *local_part;
	const char *domain;
};

static inline const char *sieve_address_to_string(const struct sieve_address *address) 
{
    if ( address == NULL || address->local_part == NULL || address->domain == NULL )
        return NULL;

    return t_strconcat(address->local_part, "@", address->domain, NULL);
}

/* 
 * RFC 2822 addresses
 */ 

bool sieve_rfc2822_mailbox_validate
	(const char *address, const char **error_r);
const char *sieve_rfc2822_mailbox_normalize
	(const char *address, const char **error_r);


const char *sieve_address_normalize
	(string_t *address, const char **error_r);
bool sieve_address_validate
	(string_t *address, const char **error_r);
	
int sieve_address_compare
	(const char *address1, const char *address2, bool normalized);

/*
 * RFC 2821 addresses (paths)
 */

const struct sieve_address *sieve_address_parse_envelope_path
	(pool_t pool, const char *field_value);

#endif
