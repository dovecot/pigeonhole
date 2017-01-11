/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "str.h"

#include "sieve-common.h"
#include "sieve-stringlist.h"

/*
 * Default implementation
 */

int sieve_stringlist_read_all
(struct sieve_stringlist *strlist, pool_t pool,
	const char * const **list_r)
{
	if ( strlist->read_all == NULL ) {
		ARRAY(const char *) items;
		string_t *item;
		int ret;

		sieve_stringlist_reset(strlist);

		p_array_init(&items, pool, 4);

		item = NULL;
		while ( (ret=sieve_stringlist_next_item(strlist, &item)) > 0 ) {
			const char *stritem = p_strdup(pool, str_c(item));

			array_append(&items, &stritem, 1);
		}

		(void)array_append_space(&items);
		*list_r = array_idx(&items, 0);

		return ( ret < 0 ? -1 : 1 );
	}

	return strlist->read_all(strlist, pool, list_r);
}

int sieve_stringlist_get_length
(struct sieve_stringlist *strlist)
{
	if ( strlist->get_length == NULL ) {
		string_t *item;
		int count = 0;
		int ret;

		sieve_stringlist_reset(strlist);
		while ( (ret=sieve_stringlist_next_item(strlist, &item)) > 0 ) {
			count++;
		}
		sieve_stringlist_reset(strlist);

		return ( ret < 0 ? -1 : count );
	}

	return strlist->get_length(strlist);
}

/*
 * Single Stringlist
 */

/* Object */

static int sieve_single_stringlist_next_item
	(struct sieve_stringlist *_strlist, string_t **str_r);
static void sieve_single_stringlist_reset
	(struct sieve_stringlist *_strlist);
static int sieve_single_stringlist_get_length
	(struct sieve_stringlist *_strlist);

struct sieve_single_stringlist {
	struct sieve_stringlist strlist;

	string_t *value;

	unsigned int end:1;
	unsigned int count_empty:1;
};

struct sieve_stringlist *sieve_single_stringlist_create
(const struct sieve_runtime_env *renv, string_t *str, bool count_empty)
{
	struct sieve_single_stringlist *strlist;

	strlist = t_new(struct sieve_single_stringlist, 1);
	strlist->strlist.runenv = renv;
	strlist->strlist.exec_status = SIEVE_EXEC_OK;
	strlist->strlist.next_item = sieve_single_stringlist_next_item;
	strlist->strlist.reset = sieve_single_stringlist_reset;
	strlist->strlist.get_length = sieve_single_stringlist_get_length;
	strlist->count_empty = count_empty;
	strlist->value = str;

	return &strlist->strlist;
}

struct sieve_stringlist *sieve_single_stringlist_create_cstr
(const struct sieve_runtime_env *renv, const char *cstr, bool count_empty)
{
	string_t *str = t_str_new_const(cstr, strlen(cstr));

	return sieve_single_stringlist_create(renv, str, count_empty);
}

/* Implementation */

static int sieve_single_stringlist_next_item
(struct sieve_stringlist *_strlist, string_t **str_r)
{
	struct sieve_single_stringlist *strlist =
		(struct sieve_single_stringlist *)_strlist;

	if ( strlist->end ) {
		*str_r = NULL;
		return 0;
	}

	*str_r = strlist->value;
	strlist->end = TRUE;
	return 1;
}

static void sieve_single_stringlist_reset
(struct sieve_stringlist *_strlist)
{
	struct sieve_single_stringlist *strlist =
		(struct sieve_single_stringlist *)_strlist;

	strlist->end = FALSE;
}

static int sieve_single_stringlist_get_length
(struct sieve_stringlist *_strlist)
{
	struct sieve_single_stringlist *strlist =
		(struct sieve_single_stringlist *)_strlist;

	return ( strlist->count_empty || str_len(strlist->value) > 0 ? 1 : 0 );
}

/*
 * Index Stringlist
 */

/* Object */

static int sieve_index_stringlist_next_item
	(struct sieve_stringlist *_strlist, string_t **str_r);
static void sieve_index_stringlist_reset
	(struct sieve_stringlist *_strlist);
static int sieve_index_stringlist_get_length
	(struct sieve_stringlist *_strlist);
static void sieve_index_stringlist_set_trace
	(struct sieve_stringlist *strlist, bool trace);

struct sieve_index_stringlist {
	struct sieve_stringlist strlist;

	struct sieve_stringlist *source;

	int index;
	unsigned int end:1;
};

struct sieve_stringlist *sieve_index_stringlist_create
(const struct sieve_runtime_env *renv, struct sieve_stringlist *source,
	int index)
{
	struct sieve_index_stringlist *strlist;

	strlist = t_new(struct sieve_index_stringlist, 1);
	strlist->strlist.runenv = renv;
	strlist->strlist.exec_status = SIEVE_EXEC_OK;
	strlist->strlist.next_item = sieve_index_stringlist_next_item;
	strlist->strlist.reset = sieve_index_stringlist_reset;
	strlist->strlist.get_length = sieve_index_stringlist_get_length;
	strlist->strlist.set_trace = sieve_index_stringlist_set_trace;
	strlist->source = source;
	strlist->index = index;

	return &strlist->strlist;
}

/* Implementation */

static int sieve_index_stringlist_next_item
(struct sieve_stringlist *_strlist, string_t **str_r)
{
	struct sieve_index_stringlist *strlist =
		(struct sieve_index_stringlist *)_strlist;
	int index, ret;

	if ( strlist->end ) {
		*str_r = NULL;
		return 0;
	}

	if ( strlist->index < 0 ) {
		int len = sieve_stringlist_get_length(strlist->source);
		if (len < 0) {
			_strlist->exec_status = strlist->source->exec_status;
			return -1;
		}

		if (len < -strlist->index) {
			*str_r = NULL;
			strlist->end = TRUE;
			return 0;
		}
		index = len + 1 + strlist->index;
	} else {
		index = strlist->index;
	}

	while ( index > 0 ) {
		if ( (ret=sieve_stringlist_next_item(strlist->source, str_r)) <= 0 ) {
			if (ret < 0)
				_strlist->exec_status = strlist->source->exec_status;
			return ret;
		}
	
		index--;
	}
		
	strlist->end = TRUE;
	return 1;
}

static void sieve_index_stringlist_reset
(struct sieve_stringlist *_strlist)
{
	struct sieve_index_stringlist *strlist =
		(struct sieve_index_stringlist *)_strlist;

	sieve_stringlist_reset(strlist->source);
	strlist->end = FALSE;
}

static int sieve_index_stringlist_get_length
(struct sieve_stringlist *_strlist)
{
	struct sieve_index_stringlist *strlist =
		(struct sieve_index_stringlist *)_strlist;
	int len;

	len = sieve_stringlist_get_length(strlist->source);
	if (len < 0) {
		_strlist->exec_status = strlist->source->exec_status;
		return -1;
	}

	if ( strlist->index < 0 ) {
		if ( -strlist->index >= len )
			return 0;
	} else if ( strlist->index >= len ) {
			return 0;
	}

	return 1;
}

static void sieve_index_stringlist_set_trace
(struct sieve_stringlist *_strlist, bool trace)
{
	struct sieve_index_stringlist *strlist =
		(struct sieve_index_stringlist *)_strlist;

	sieve_stringlist_set_trace(strlist->source, trace);
}
