/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-common.h"
#include "sieve-error.h"

#include "sieve-data-storage.h"

/*
 * Storage class
 */

static struct sieve_storage *sieve_data_storage_alloc(void)
{
	struct sieve_data_storage *dstorage;
	pool_t pool;

	pool = pool_alloconly_create("sieve_data_storage", 1024);
	dstorage = p_new(pool, struct sieve_data_storage, 1);
	dstorage->storage = sieve_data_storage;
	dstorage->storage.pool = pool;

	return &dstorage->storage;
}

static int
sieve_data_storage_init(struct sieve_storage *storage ATTR_UNUSED)
{
	return 0;
}

/*
 * Driver definition
 */

const struct sieve_storage sieve_data_storage = {
	.driver_name = SIEVE_DATA_STORAGE_DRIVER_NAME,
	.version = 0,
	.v = {
		.alloc = sieve_data_storage_alloc,
		.init = sieve_data_storage_init,
	},
};
