#ifndef SIEVE_DATA_STORAGE_H
#define SIEVE_DATA_STORAGE_H

#include "sieve.h"
#include "sieve-script-private.h"
#include "sieve-storage-private.h"

/*
 * Storage class
 */

struct sieve_data_storage {
	struct sieve_storage storage;
};

/*
 * Script class
 */

struct sieve_data_script {
	struct sieve_script script;

	struct istream *data;
};

struct sieve_script *
sieve_data_script_create_from_input(struct sieve_instance *svinst,
				    const char *cause, const char *name,
				    struct istream *input);

#endif
