/* Copyright (c) 2002-2016 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SIEVE_ADDRESS_SOURCE_H
#define __SIEVE_ADDRESS_SOURCE_H

#include "sieve-common.h"

enum sieve_address_source_type {
	SIEVE_ADDRESS_SOURCE_DEFAULT = 0,
	SIEVE_ADDRESS_SOURCE_SENDER,
	SIEVE_ADDRESS_SOURCE_RECIPIENT,
	SIEVE_ADDRESS_SOURCE_ORIG_RECIPIENT,
	SIEVE_ADDRESS_SOURCE_POSTMASTER,
	SIEVE_ADDRESS_SOURCE_EXPLICIT
};

struct sieve_address_source {
	enum sieve_address_source_type type;
	const struct sieve_address *address;
};

bool sieve_address_source_parse
	(pool_t pool, const char *value,
		struct sieve_address_source *asrc);
bool sieve_address_source_parse_from_setting
	(struct sieve_instance *svinst, pool_t pool,
		const char *setting, struct sieve_address_source *asrc);

#endif
