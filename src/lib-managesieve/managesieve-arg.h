/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#ifndef __MANAGESIEVE_ARG_H
#define __MANAGESIEVE_ARG_H

#include "array.h"

/*
 * QUOTED-SPECIALS    = <"> / "\"
 */
#define IS_QUOTED_SPECIAL(c) \
	((c) == '"' || (c) == '\\')

/*
 * ATOM-SPECIALS      = "(" / ")" / "{" / SP / CTL / QUOTED-SPECIALS
 */
#define IS_ATOM_SPECIAL(c) \
	((c) == '(' || (c) == ')' || (c) == '{' || \
	 (c) <= 32 || (c) == 0x7f || \
	 IS_QUOTED_SPECIAL(c))

/*
 * CHAR               = %x01-7F
 */
#define IS_CHAR(c) \
	(((c) & 0x80) == 0)

/*
 * TEXT-CHAR          = %x01-09 / %x0B-0C / %x0E-7F
 *                       ;; any CHAR except CR and LF
 */
#define IS_TEXT_CHAR(c) \
	(IS_CHAR(c) && (c) != '\r' && (c) != '\n')

/*
 * SAFE-CHAR          = %x01-09 / %x0B-0C / %x0E-21 /
 *                      %x23-5B / %x5D-7F
 *                      ;; any TEXT-CHAR except QUOTED-SPECIALS
 */
#define IS_SAFE_CHAR(c) \
	(IS_TEXT_CHAR(c) && !IS_QUOTED_SPECIAL(c))

enum managesieve_arg_type {
	MANAGESIEVE_ARG_NONE = 0,
	MANAGESIEVE_ARG_ATOM,
	MANAGESIEVE_ARG_STRING,
	MANAGESIEVE_ARG_STRING_STREAM,

	MANAGESIEVE_ARG_LIST,

	/* literals are returned as MANAGESIEVE_ARG_STRING by default */
	MANAGESIEVE_ARG_LITERAL,

	MANAGESIEVE_ARG_EOL /* end of argument list */
};

ARRAY_DEFINE_TYPE(managesieve_arg_list, struct managesieve_arg);
struct managesieve_arg {
	enum managesieve_arg_type type;
	struct managesieve_arg *parent; /* always of type MANAGESIEVE_ARG_LIST */

	/* Set when _data.str is set */
	size_t str_len;

	union {
		const char *str;
		struct istream *str_stream;
		ARRAY_TYPE(managesieve_arg_list) list;
	} _data;
};

#define MANAGESIEVE_ARG_IS_EOL(arg) \
	((arg)->type == MANAGESIEVE_ARG_EOL)

bool managesieve_arg_get_atom
	(const struct managesieve_arg *arg, const char **str_r)
	ATTR_WARN_UNUSED_RESULT;
bool managesieve_arg_get_number
	(const struct managesieve_arg *arg, uoff_t *number_r)
	ATTR_WARN_UNUSED_RESULT;
bool managesieve_arg_get_quoted
	(const struct managesieve_arg *arg, const char **str_r)
	ATTR_WARN_UNUSED_RESULT;
bool managesieve_arg_get_string
	(const struct managesieve_arg *arg, const char **str_r)
	ATTR_WARN_UNUSED_RESULT;

bool managesieve_arg_get_string_stream
	(const struct managesieve_arg *arg, struct istream **stream_r)
	ATTR_WARN_UNUSED_RESULT;

bool managesieve_arg_get_list
	(const struct managesieve_arg *arg, const struct managesieve_arg **list_r)
	ATTR_WARN_UNUSED_RESULT;
bool managesieve_arg_get_list_full
	(const struct managesieve_arg *arg, const struct managesieve_arg **list_r,
		unsigned int *list_count_r)
	ATTR_WARN_UNUSED_RESULT;

/* Similar to above, but assumes that arg is already of correct type. */
struct istream *managesieve_arg_as_string_stream
	(const struct managesieve_arg *arg);
const struct managesieve_arg *managesieve_arg_as_list
	(const struct managesieve_arg *arg);

/* Returns TRUE if arg is atom and case-insensitively matches str */
bool managesieve_arg_atom_equals
	(const struct managesieve_arg *arg, const char *str);

#endif
