/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file */

#include "lib.h"
#include "strtrim.h"

enum _str_trim_sides {
	STR_TRIM_LEFT = BIT(0),
	STR_TRIM_RIGHT = BIT(1),
};

static void ph_str_trim_parse(const char *str,
	const char *chars, enum _str_trim_sides sides, 
	const char **begin_r, const char **end_r)
{
	const char *p, *pend, *begin, *end;

	*begin_r = *end_r = NULL;

	pend = str + strlen(str);
	if (pend == str)
		return;

	p = str;
	if ((sides & STR_TRIM_LEFT) != 0) {
		while (p < pend && strchr(chars, *p) != NULL)
			p++;
		if (p == pend)
			return;
	}
	begin = p;

	p = pend;
	if ((sides & STR_TRIM_RIGHT) != 0) {
		while (p > begin && strchr(chars, *(p-1)) != NULL)
			p--;
		if (p == begin)
			return;
	}
	end = p;

	*begin_r = begin;
	*end_r = end;
}

const char *ph_t_str_trim(const char *str, const char *chars)
{
	const char *begin, *end;

	ph_str_trim_parse(str, chars,
		STR_TRIM_LEFT | STR_TRIM_RIGHT, &begin, &end);
	if (begin == NULL)
		return "";
	return t_strdup_until(begin, end);
}

const char *ph_p_str_trim(pool_t pool, const char *str, const char *chars)
{
	const char *begin, *end;

	ph_str_trim_parse(str, chars,
		STR_TRIM_LEFT | STR_TRIM_RIGHT, &begin, &end);
	if (begin == NULL)
		return "";
	return p_strdup_until(pool, begin, end);
}

const char *ph_str_ltrim(const char *str, const char *chars)
{
	const char *begin, *end;

	ph_str_trim_parse(str, chars, STR_TRIM_LEFT, &begin, &end);
	if (begin == NULL)
		return "";
	return begin;
}

const char *ph_t_str_ltrim(const char *str, const char *chars)
{
	return t_strdup(str_ltrim(str, chars));
}

const char *ph_p_str_ltrim(pool_t pool, const char *str, const char *chars)
{
	return p_strdup(pool, str_ltrim(str, chars));
}

const char *ph_t_str_rtrim(const char *str, const char *chars)
{
	const char *begin, *end;

	ph_str_trim_parse(str, chars, STR_TRIM_RIGHT, &begin, &end);
	if (begin == NULL)
		return "";
	return t_strdup_until(begin, end);
}

const char *ph_p_str_rtrim(pool_t pool, const char *str, const char *chars)
{
	const char *begin, *end;

	ph_str_trim_parse(str, chars, STR_TRIM_RIGHT, &begin, &end);
	if (begin == NULL)
		return "";
	return p_strdup_until(pool, begin, end);
}

