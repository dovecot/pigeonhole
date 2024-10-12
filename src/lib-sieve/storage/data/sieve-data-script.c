/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "strfuncs.h"
#include "istream.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-dump.h"
#include "sieve-binary.h"

#include "sieve-data-storage.h"

/*
 * Script data implementation
 */

static struct sieve_data_script *sieve_data_script_alloc(void)
{
	struct sieve_data_script *dscript;
	pool_t pool;

	pool = pool_alloconly_create("sieve_data_script", 1024);
	dscript = p_new(pool, struct sieve_data_script, 1);
	dscript->script = sieve_data_script;
	dscript->script.pool = pool;

	return dscript;
}

struct sieve_script *
sieve_data_script_create_from_input(struct sieve_instance *svinst,
				    const char *name, struct istream *input)
{
	struct sieve_storage *storage;
	struct sieve_data_script *dscript = NULL;
	int ret;

	ret = sieve_storage_alloc(svinst, NULL, &sieve_data_storage,
				  "", 0, FALSE, &storage, NULL, NULL);
	i_assert(ret >= 0);

	dscript = sieve_data_script_alloc();
	sieve_script_init(&dscript->script, storage, &sieve_data_script,
			  "data:", name);

	dscript->data = input;
	i_stream_ref(dscript->data);

	sieve_storage_unref(&storage);

	dscript->script.open = TRUE;

	return &dscript->script;
}

static void sieve_data_script_destroy(struct sieve_script *script)
{
	struct sieve_data_script *dscript =
		container_of(script, struct sieve_data_script, script);

	i_stream_unref(&dscript->data);
}

static int
sieve_data_script_get_stream(struct sieve_script *script,
			     struct istream **stream_r)
{
	struct sieve_data_script *dscript =
		container_of(script, struct sieve_data_script, script);

	i_stream_ref(dscript->data);
	i_stream_seek(dscript->data, 0);

	*stream_r = dscript->data;
	return 0;
}

const struct sieve_script sieve_data_script = {
	.driver_name = SIEVE_DATA_STORAGE_DRIVER_NAME,
	.v = {
		.destroy = sieve_data_script_destroy,

		.get_stream = sieve_data_script_get_stream,
	},
};
