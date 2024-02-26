/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "mempool.h"
#include "array.h"

#include "rfc2822.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-settings.old.h"
#include "sieve-extensions.h"

#include "ext-editheader-limits.h"
#include "ext-editheader-common.h"

/*
 * Extension configuration
 */

struct ext_editheader_header {
	const char *name;

	bool forbid_add:1;
	bool forbid_delete:1;
};

struct ext_editheader_context {
	pool_t pool;

	ARRAY(struct ext_editheader_header) headers;

	size_t max_header_size;
};

static struct ext_editheader_header *
ext_editheader_config_header_find(struct ext_editheader_context *extctx,
				  const char *hname)
{
	struct ext_editheader_header *headers;
	unsigned int count, i;

	headers = array_get_modifiable(&extctx->headers, &count);
	for (i = 0; i < count; i++) {
		if (strcasecmp(hname, headers[i].name) == 0)
			return &headers[i];
	}
	return NULL;
}

static void
ext_editheader_config_headers(struct sieve_instance *svinst,
			      struct ext_editheader_context *extctx,
			      const char *setting, bool forbid_add,
			      bool forbid_delete)
{
	const char *setval;

	setval = sieve_setting_get(svinst, setting);
	if (setval != NULL) {
		const char **headers = t_strsplit_spaces(setval, " \t");

		while (*headers != NULL) {
			struct ext_editheader_header *header;

			if (!rfc2822_header_field_name_verify(
				*headers, strlen(*headers))) {
				e_warning(svinst->event, "editheader: "
					  "setting %s contains invalid header field name "
					  "'%s' (ignored)",
					  setting, *headers);
				headers++;
				continue;
			}

			header = ext_editheader_config_header_find(
				extctx, *headers);
			if (header == NULL) {
				header = array_append_space(
					&extctx->headers);
				header->name = p_strdup(extctx->pool,
							*headers);
			}

			if (forbid_add)
				header->forbid_add = TRUE;
			if (forbid_delete)
				header->forbid_delete = TRUE;

			headers++;
		}
	}
}

int ext_editheader_load(const struct sieve_extension *ext, void **context_r)
{
	struct ext_editheader_context *extctx;
	struct sieve_instance *svinst = ext->svinst;
	size_t max_header_size;
	pool_t pool;

	T_BEGIN {
		pool = pool_alloconly_create("editheader_config", 1024);
		extctx = p_new(pool, struct ext_editheader_context, 1);
		extctx->pool = pool;
		extctx->max_header_size =
			EXT_EDITHEADER_DEFAULT_MAX_HEADER_SIZE;

		p_array_init(&extctx->headers, pool, 16);

		ext_editheader_config_headers(
			svinst, extctx,
			"sieve_editheader_protected", TRUE, TRUE);
		ext_editheader_config_headers(
			svinst, extctx,
			"sieve_editheader_forbid_add", TRUE, FALSE);
		ext_editheader_config_headers(
			svinst, extctx,
			"sieve_editheader_forbid_delete", FALSE, TRUE);

		if (sieve_setting_get_size_value(
			svinst, "sieve_editheader_max_header_size",
			&max_header_size)) {
			if (max_header_size < EXT_EDITHEADER_MINIMUM_MAX_HEADER_SIZE) {
				e_warning(svinst->event, "editheader: "
					  "value of sieve_editheader_max_header_size setting "
					  "(=%zu) is less than the minimum (=%zu) "
					  "(ignored)", max_header_size,
					  (size_t)EXT_EDITHEADER_MINIMUM_MAX_HEADER_SIZE);
			} else {
				extctx->max_header_size = max_header_size;
			}
		}
	} T_END;

	*context_r = extctx;
	return 0;
}

void ext_editheader_unload(const struct sieve_extension *ext)
{
	struct ext_editheader_context *extctx = ext->context;

	if (extctx != NULL)
		pool_unref(&extctx->pool);
}

/*
 * Protected headers
 */

bool ext_editheader_header_allow_add(const struct sieve_extension *ext,
				     const char *hname)
{
	struct ext_editheader_context *extctx = ext->context;
	const struct ext_editheader_header *header;

	if (strcasecmp(hname, "subject") == 0)
		return TRUE;
	if (strcasecmp(hname, "x-sieve-redirected-from") == 0)
		return FALSE;

	header = ext_editheader_config_header_find(extctx, hname);
	if (header == NULL)
		return TRUE;

	return !header->forbid_add;
}

bool ext_editheader_header_allow_delete(const struct sieve_extension *ext,
					const char *hname)
{
	struct ext_editheader_context *extctx = ext->context;
	const struct ext_editheader_header *header;

	if (strcasecmp(hname, "received") == 0 ||
	    strcasecmp(hname, "auto-submitted") == 0)
		return FALSE;
	if (strcasecmp(hname, "x-sieve-redirected-from") == 0)
		return FALSE;
	if (strcasecmp(hname, "subject") == 0)
		return TRUE;

	header = ext_editheader_config_header_find(extctx, hname);
	if (header == NULL)
		return TRUE;

	return !header->forbid_delete;
}

/*
 * Limits
 */

bool ext_editheader_header_too_large(const struct sieve_extension *ext,
				     size_t size)
{
	struct ext_editheader_context *extctx = ext->context;

	return size > extctx->max_header_size;
}
