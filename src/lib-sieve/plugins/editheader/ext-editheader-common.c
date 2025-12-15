/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "mempool.h"
#include "array.h"
#include "settings.h"

#include "rfc2822.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-extensions.h"

#include "ext-editheader-limits.h"
#include "ext-editheader-settings.h"
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
	const struct ext_editheader_settings *set;

	ARRAY(struct ext_editheader_header) headers;

	size_t max_header_size;
};

static const struct ext_editheader_header *
ext_editheader_header_find(struct ext_editheader_context *extctx,
			   const char *hname)
{
	const struct ext_editheader_header *header;

	if (extctx == NULL)
		return NULL;

	array_foreach(&extctx->headers, header) {
		if (strcasecmp(hname, header->name) == 0)
			return header;
	}
	return NULL;
}

static int
ext_editheader_header_add(struct sieve_instance *svinst,
			  struct ext_editheader_context *extctx,
			  const char *hname)
{
	struct ext_editheader_header *header;
	const struct ext_editheader_header_settings *set;
	const char *error;

	if (settings_get_filter(svinst->event, "sieve_editheader_header", hname,
				&ext_editheader_header_setting_parser_info, 0,
				&set, &error) < 0) {
		e_error(svinst->event, "%s", error);
		return -1;
	}

	i_assert(ext_editheader_header_find(extctx, hname) == NULL);

	header = array_append_space(&extctx->headers);
	header->name = p_strdup(extctx->pool, hname);
	header->forbid_add = set->forbid_add;
	header->forbid_delete = set->forbid_delete;

	settings_free(set);
	return 0;
}

static int
ext_editheader_config_headers(struct sieve_instance *svinst,
			      struct ext_editheader_context *extctx)
{
	const char *hname;

	if (!array_is_created(&extctx->set->headers))
		return 0;

	array_foreach_elem(&extctx->set->headers, hname) {
		if (ext_editheader_header_add(svinst, extctx, hname) < 0)
			return -1;
	}
	return 0;
}

int ext_editheader_load(const struct sieve_extension *ext, void **context_r)
{
	struct sieve_instance *svinst = ext->svinst;
	const struct ext_editheader_settings *set;
	struct ext_editheader_context *extctx;
	const char *error;
	pool_t pool;

	if (settings_get(svinst->event, &ext_editheader_setting_parser_info, 0,
			 &set, &error) < 0) {
		e_error(svinst->event, "%s", error);
		return -1;
	}

	pool = pool_alloconly_create("editheader_config", 1024);
	extctx = p_new(pool, struct ext_editheader_context, 1);
	extctx->pool = pool;
	extctx->set = set;
	p_array_init(&extctx->headers, pool, 16);

	if (ext_editheader_config_headers(svinst, extctx) < 0) {
		settings_free(set);
		pool_unref(&pool);
		return -1;
	}

	*context_r = extctx;
	return 0;
}

void ext_editheader_unload(const struct sieve_extension *ext)
{
	struct ext_editheader_context *extctx = ext->context;

	if (extctx == NULL)
		return;
	settings_free(extctx->set);
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

	header = ext_editheader_header_find(extctx, hname);
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

	header = ext_editheader_header_find(extctx, hname);
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

	if (extctx == NULL)
		return size > EXT_EDITHEADER_DEFAULT_MAX_HEADER_SIZE;

	return (size > extctx->set->max_header_size);
}
