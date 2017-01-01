/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "mempool.h"
#include "buffer.h"
#include "array.h"
#include "str.h"
#include "istream.h"
#include "mail-storage.h"

#include "sieve-common.h"
#include "sieve-stringlist.h"
#include "sieve-code.h"
#include "sieve-message.h"
#include "sieve-interpreter.h"

#include "ext-body-common.h"


/*
 * Body part stringlist
 */

static int ext_body_stringlist_next_item
	(struct sieve_stringlist *_strlist, string_t **str_r);
static void ext_body_stringlist_reset
	(struct sieve_stringlist *_strlist);

struct ext_body_stringlist {
	struct sieve_stringlist strlist;

	struct sieve_message_part_data *body_parts;
	struct sieve_message_part_data *body_parts_iter;
};

int ext_body_get_part_list
(const struct sieve_runtime_env *renv, enum tst_body_transform transform,
	const char * const *content_types, struct sieve_stringlist **strlist_r)
{
	static const char * const _no_content_types[] = { "", NULL };
	struct ext_body_stringlist *strlist;
	struct sieve_message_part_data *body_parts = NULL;
	int ret;

	*strlist_r = NULL;

	if ( content_types == NULL ) content_types = _no_content_types;

	switch ( transform ) {
	case TST_BODY_TRANSFORM_RAW:
		if ( (ret=sieve_message_body_get_raw(renv, &body_parts)) <= 0 )
			return ret;
		break;
	case TST_BODY_TRANSFORM_CONTENT:
		if ( (ret=sieve_message_body_get_content
			(renv, content_types, &body_parts)) <= 0 )
			return ret;
		break;
	case TST_BODY_TRANSFORM_TEXT:
		if ( (ret=sieve_message_body_get_text(renv, &body_parts)) <= 0 )
			return ret;
		break;
	default:
		i_unreached();
	}

	strlist = t_new(struct ext_body_stringlist, 1);
	strlist->strlist.runenv = renv;
	strlist->strlist.next_item = ext_body_stringlist_next_item;
	strlist->strlist.reset = ext_body_stringlist_reset;
	strlist->body_parts = body_parts;
	strlist->body_parts_iter = body_parts;

	*strlist_r = &strlist->strlist;
	return SIEVE_EXEC_OK;
}

static int ext_body_stringlist_next_item
(struct sieve_stringlist *_strlist, string_t **str_r)
{
	struct ext_body_stringlist *strlist =
		(struct ext_body_stringlist *)_strlist;

	*str_r = NULL;

	if ( strlist->body_parts_iter->content == NULL ) return 0;

	*str_r = t_str_new_const
		(strlist->body_parts_iter->content, strlist->body_parts_iter->size);
	strlist->body_parts_iter++;
	return 1;
}

static void ext_body_stringlist_reset
(struct sieve_stringlist *_strlist)
{
	struct ext_body_stringlist *strlist =
		(struct ext_body_stringlist *)_strlist;

	strlist->body_parts_iter = strlist->body_parts;
}
