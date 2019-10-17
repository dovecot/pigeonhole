/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "strescape.h"
#include "managesieve-arg.h"

bool managesieve_arg_get_atom(const struct managesieve_arg *arg,
			      const char **str_r)
{
	if (arg->type != MANAGESIEVE_ARG_ATOM)
		return FALSE;

	*str_r = arg->_data.str;
	return TRUE;
}

bool managesieve_arg_get_number(const struct managesieve_arg *arg,
				uoff_t *number_r)
{
	const char *data;
	uoff_t num = 0;
	size_t i;

	if (arg->type != MANAGESIEVE_ARG_ATOM)
		return FALSE;

	data = arg->_data.str;
	for (i = 0; i < arg->str_len; i++) {
		uoff_t newnum;

		if (data[i] < '0' || data[i] > '9')
			return FALSE;

		newnum = num*10 + (data[i] -'0');
		if (newnum < num)
			return FALSE;

		num = newnum;
	}

	*number_r = num;
	return TRUE;
}

bool managesieve_arg_get_quoted(const struct managesieve_arg *arg,
				const char **str_r)
{
	if (arg->type != MANAGESIEVE_ARG_STRING)
		return FALSE;

	*str_r = arg->_data.str;
	return TRUE;
}

bool managesieve_arg_get_string(const struct managesieve_arg *arg,
				const char **str_r)
{
	if (arg->type != MANAGESIEVE_ARG_STRING &&
	    arg->type != MANAGESIEVE_ARG_LITERAL)
		return FALSE;

	*str_r = arg->_data.str;
	return TRUE;
}

bool managesieve_arg_get_string_stream(const struct managesieve_arg *arg,
				       struct istream **stream_r)
{
	if (arg->type != MANAGESIEVE_ARG_STRING_STREAM)
		return FALSE;

	*stream_r = arg->_data.str_stream;
	return TRUE;
}

bool managesieve_arg_get_list(const struct managesieve_arg *arg,
			      const struct managesieve_arg **list_r)
{
	unsigned int count;

	return managesieve_arg_get_list_full(arg, list_r, &count);
}

bool managesieve_arg_get_list_full(const struct managesieve_arg *arg,
				   const struct managesieve_arg **list_r,
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

const char *managesieve_arg_as_atom(const struct managesieve_arg *arg)
{
	const char *str;

	if (!managesieve_arg_get_atom(arg, &str))
		i_unreached();
	return str;
}

const char *managesieve_arg_as_string(const struct managesieve_arg *arg)
{
	const char *str;

	if (!managesieve_arg_get_string(arg, &str))
		i_unreached();
	return str;
}

struct istream *
managesieve_arg_as_string_stream(const struct managesieve_arg *arg)
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

bool managesieve_arg_atom_equals(const struct managesieve_arg *arg,
				 const char *str)
{
	const char *value;

	if (!managesieve_arg_get_atom(arg, &value))
		return FALSE;
	return strcasecmp(value, str) == 0;
}

void managesieve_write_arg(string_t *dest, const struct managesieve_arg *arg)
{
	const char *strval;

	switch (arg->type) {
	case MANAGESIEVE_ARG_ATOM:
		str_append(dest, managesieve_arg_as_atom(arg));
		break;
	case MANAGESIEVE_ARG_STRING:
		strval = managesieve_arg_as_string(arg);
		str_append_c(dest, '"');
		str_append_escaped(dest, strval, strlen(strval));
		str_append_c(dest, '"');
		break;
	case MANAGESIEVE_ARG_STRING_STREAM:
		str_append(dest, "\"<too large>\"");
		break;
	case MANAGESIEVE_ARG_LITERAL: {
		const char *strarg = managesieve_arg_as_string(arg);
		str_printfa(dest, "{%"PRIuSIZE_T"}\r\n",
			    strlen(strarg));
		str_append(dest, strarg);
		break;
	}
	case MANAGESIEVE_ARG_LIST:
		str_append_c(dest, '(');
		managesieve_write_args(dest, managesieve_arg_as_list(arg));
		str_append_c(dest, ')');
		break;
	case MANAGESIEVE_ARG_NONE:
	case MANAGESIEVE_ARG_EOL:
		i_unreached();
	}
}

void managesieve_write_args(string_t *dest, const struct managesieve_arg *args)
{
	bool first = TRUE;

	for (; !MANAGESIEVE_ARG_IS_EOL(args); args++) {
		if (first)
			first = FALSE;
		else
			str_append_c(dest, ' ');
		managesieve_write_arg(dest, args);
	}
}

const char *managesieve_args_to_str(const struct managesieve_arg *args)
{
	string_t *str;

	str = t_str_new(128);
	managesieve_write_args(str, args);
	return str_c(str);
}

