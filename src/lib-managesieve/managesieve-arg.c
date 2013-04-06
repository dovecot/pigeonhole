/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "managesieve-arg.h"

bool managesieve_arg_get_atom
(const struct managesieve_arg *arg, const char **str_r)
{
	if (arg->type != MANAGESIEVE_ARG_ATOM)
		return FALSE;

	*str_r = arg->_data.str;
	return TRUE;
}

bool managesieve_arg_get_number
(const struct managesieve_arg *arg, uoff_t *number_r)
{
	const char *data;
	uoff_t num = 0;
	size_t i;

	if ( arg->type != MANAGESIEVE_ARG_ATOM )
		return FALSE;

	data = arg->_data.str;
	for ( i = 0; i < arg->str_len; i++ ) {
		uoff_t newnum;

		if (data[i] < '0' || data[i] > '9')
			return FALSE;

		newnum = num*10 + (data[i] -'0');
		if ( newnum < num )
			return FALSE;

		num = newnum;
	}

	*number_r = num;
	return TRUE;
}

bool managesieve_arg_get_quoted
(const struct managesieve_arg *arg, const char **str_r)
{
	if (arg->type != MANAGESIEVE_ARG_STRING)
		return FALSE;

	*str_r = arg->_data.str;
	return TRUE;
}

bool managesieve_arg_get_string
(const struct managesieve_arg *arg, const char **str_r)
{
	if (arg->type != MANAGESIEVE_ARG_STRING
		&& arg->type != MANAGESIEVE_ARG_LITERAL)
		return FALSE;

	*str_r = arg->_data.str;
	return TRUE;
}

bool managesieve_arg_get_string_stream
(const struct managesieve_arg *arg, struct istream **stream_r)
{
	if ( arg->type != MANAGESIEVE_ARG_STRING_STREAM )
		return FALSE;

	*stream_r = arg->_data.str_stream;
	return TRUE;
}

bool managesieve_arg_get_list
(const struct managesieve_arg *arg, const struct managesieve_arg **list_r)
{
	unsigned int count;

	return managesieve_arg_get_list_full(arg, list_r, &count);
}

bool managesieve_arg_get_list_full
(const struct managesieve_arg *arg, const struct managesieve_arg **list_r,
	unsigned int *list_count_r)
{
	unsigned int count;

	if (arg->type != MANAGESIEVE_ARG_LIST)
		return FALSE;

	*list_r = array_get(&arg->_data.list, &count);

	/* drop MANAGESIEVE_ARG_EOL from list size */
	i_assert(count > 0);
	*list_count_r = count - 1;
	return TRUE;
}

struct istream *managesieve_arg_as_string_stream
(const struct managesieve_arg *arg)
{
	struct istream *stream;

	if (!managesieve_arg_get_string_stream(arg, &stream))
		i_unreached();
	return stream;
}

const struct managesieve_arg *
managesieve_arg_as_list(const struct managesieve_arg *arg)
{
	const struct managesieve_arg *ret;

	if (!managesieve_arg_get_list(arg, &ret))
		i_unreached();
	return ret;
}

bool managesieve_arg_atom_equals
(const struct managesieve_arg *arg, const char *str)
{
	const char *value;

	if (!managesieve_arg_get_atom(arg, &value))
		return FALSE;
	return strcasecmp(value, str) == 0;
}
