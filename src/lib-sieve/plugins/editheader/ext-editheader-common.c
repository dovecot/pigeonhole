/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "mempool.h"
#include "array.h"

#include "rfc2822.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-settings.h"
#include "sieve-extensions.h"

#include "ext-editheader-limits.h"
#include "ext-editheader-common.h"

/*
 * Extension configuration
 */

struct ext_editheader_header {
	const char *name;

	/* may extend this later */
	unsigned int protected:1;
};

struct ext_editheader_config {
	pool_t pool;

	ARRAY(struct ext_editheader_header) headers;

	size_t max_header_size;
};

static struct ext_editheader_header *ext_editheader_config_header_find
(struct ext_editheader_config *ext_config, const char *hname)
{
	struct ext_editheader_header *headers;
	unsigned int count, i;

	headers = array_get_modifiable(&ext_config->headers, &count);
	for ( i = 0; i < count; i++ ) {
		if ( strcasecmp(hname, headers[i].name) == 0 )
			return &headers[i];
	}

	return NULL;
}

bool ext_editheader_load
(const struct sieve_extension *ext, void **context)
{
	struct ext_editheader_config *ext_config;
	struct sieve_instance *svinst = ext->svinst;
	const char *protected;
	size_t max_header_size;
	pool_t pool;

	if ( *context != NULL ) {
		ext_editheader_unload(ext);
		*context = NULL;
	}

	T_BEGIN {
		pool = pool_alloconly_create("editheader_config", 1024);
		ext_config = p_new(pool, struct ext_editheader_config, 1);
		ext_config->pool = pool;
		ext_config->max_header_size = EXT_EDITHEADER_DEFAULT_MAX_HEADER_SIZE;

		p_array_init(&ext_config->headers, pool, 16);

		protected = sieve_setting_get(svinst, "sieve_editheader_protected");
		if ( protected != NULL ) {
			const char **headers = t_strsplit_spaces(protected, " \t");

			while ( *headers != NULL ) {
				struct ext_editheader_header *header;

				if ( !rfc2822_header_field_name_verify(*headers, strlen(*headers)) ) {
					sieve_sys_warning(svinst,
						"editheader: setting sieve_editheader_protected contains "
						"invalid header field name `%s' (ignored)", *headers);
					continue;
				}

				header=ext_editheader_config_header_find(ext_config, *headers);
				if ( header == NULL ) {
					header = array_append_space(&ext_config->headers);
					header->name = p_strdup(pool, *headers);
				}

				header->protected = TRUE;

				headers++;
			}
		}

		if ( sieve_setting_get_size_value
			(svinst, "sieve_editheader_max_header_size", &max_header_size) ) {
			if ( max_header_size < EXT_EDITHEADER_MINIMUM_MAX_HEADER_SIZE ) {
				sieve_sys_warning(svinst,
					"editheader: value of sieve_editheader_max_header_size setting "
					"(=%"PRIuSIZE_T") is less than the minimum (=%"PRIuSIZE_T") "
					"(ignored)", max_header_size,
					(size_t) EXT_EDITHEADER_MINIMUM_MAX_HEADER_SIZE);
			} else {
				ext_config->max_header_size = max_header_size;
			}
		}
	} T_END;

	*context = (void *) ext_config;
	return TRUE;
}

void ext_editheader_unload(const struct sieve_extension *ext)
{
	struct ext_editheader_config *ext_config =
		(struct ext_editheader_config *) ext->context;

	if ( ext_config != NULL ) {
		pool_unref(&ext_config->pool);
	}
}

/*
 * Protected headers
 */

bool ext_editheader_header_is_protected
(const struct sieve_extension *ext, const char *hname)
{
	struct ext_editheader_config *ext_config =
		(struct ext_editheader_config *) ext->context;
	const struct ext_editheader_header *header;

	if ( strcasecmp(hname, "received") == 0
		|| strcasecmp(hname, "auto-submitted") == 0 ) {
		return TRUE;
	}

	if ( strcasecmp(hname, "subject") == 0 ) {
		return FALSE;
	}

	if ( (header=ext_editheader_config_header_find(ext_config, hname)) == NULL )
		return FALSE;

	return header->protected;
}

/*
 * Limits
 */

bool ext_editheader_header_too_large
(const struct sieve_extension *ext, size_t size)
{
	struct ext_editheader_config *ext_config =
		(struct ext_editheader_config *) ext->context;

	return size > ext_config->max_header_size;
}

