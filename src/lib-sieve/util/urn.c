/* Copyright (c) 2025 Pigeonhole authors, see the included COPYING file */

#include "lib.h"
#include "str.h"
#include "uri-util.h"

#include "urn.h"

/* RFC 8141, Section 2

   namestring    = assigned-name
                 [ rq-components ]
		 [ "#" f-component ]
   assigned-name = "urn" ":" NID ":" NSS
   NID           = (alphanum) 0*30(ldh) (alphanum)
   ldh           = alphanum / "-"
   NSS           = pchar *(pchar / "/")
   rq-components = [ "?+" r-component ]
                 [ "?=" q-component ]
   r-component   = pchar *( pchar / "/" / "?" )
   q-component   = pchar *( pchar / "/" / "?" )
   f-component   = fragment
 */

/*
 * URN parser
 */

struct urn_parser {
	struct uri_parser parser;

	enum urn_parse_flags flags;

	struct urn *urn;

	bool normalizing:1;
};

const uint16_t urn_alphanum_char_mask = BIT(0);
const uint16_t urn_pchar_char_mask = BIT(0) | BIT(1);
const uint16_t urn_pchar_slash_char_mask = BIT(0) | BIT(1) | BIT(2);
const uint16_t urn_component_char_mask = BIT(0) | BIT(1) | BIT(2) | BIT(3);

static unsigned const char urn_char_lookup[256] = {
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 00
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 10
	 0,  2,  0,  0,  2,  0,  2,  2,  0,  0,  2,  2,  2,  2,  2,  4,  // 20
	 1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  0,  2,  0,  8,  // 30
	 2,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 40
	 1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  0,  2,  // 50
	 0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 60
	 1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  0,  2,  2,  0,  // 70

	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 80
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 90
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // a0
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // b0
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // c0
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // d0
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // e0
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // f0
};

static inline bool urn_char_is_alphanum(unsigned const char ch)
{
	return ((urn_char_lookup[ch] & urn_alphanum_char_mask) != 0);
}

static inline bool urn_char_is_pchar(unsigned const char ch)
{
	return ((urn_char_lookup[ch] & urn_pchar_char_mask) != 0);
}

static inline bool urn_char_is_pchar_slash(unsigned const char ch)
{
	return ((urn_char_lookup[ch] & urn_pchar_slash_char_mask) != 0);
}

static inline bool urn_char_is_component(unsigned const char ch)
{
	return ((urn_char_lookup[ch] & urn_component_char_mask) != 0);
}

static int
urn_parse_scheme(struct urn_parser *urn_parser, const char **scheme_r)
{
	struct uri_parser *parser = &urn_parser->parser;

	*scheme_r = NULL;
	if ((urn_parser->flags & URN_PARSE_SCHEME_EXTERNAL) != 0)
		return 0;

	if (uri_parse_scheme(parser, scheme_r) <= 0) {
		parser->cur = parser->begin;
		return -1;
	}

	return 1;
}

static int uri_parse_nid(struct urn_parser *urn_parser)
{
	struct uri_parser *parser = &urn_parser->parser;
	struct urn *urn = urn_parser->urn;
	const unsigned char *first = parser->cur;

	/* NID = (alphanum) 0*30(ldh) (alphanum)
	 */

	if (parser->cur >= parser->end) {
		parser->error = "URN is empty";
		return -1;
	}

	/* alphanum */
	if (!urn_char_is_alphanum(*parser->cur)) {
		parser->error = p_strdup_printf(parser->pool,
			"URN NID begins with invalid character %s",
			uri_char_sanitize(*parser->cur));
		return -1;
	}
	parser->cur++;

	/* 0*30(ldh) */
	while (parser->cur < parser->end) {
		if (!urn_char_is_alphanum(*parser->cur) && *parser->cur != '-')
			break;
		if ((parser->cur - first) > 32)
			break;
		parser->cur++;
	}

	/* alphanum */
	if (parser->cur >= parser->end) {
		parser->error = "URN ends without NSS";
		return -1;
	}
	if (*parser->cur != ':') {
		if ((parser->cur - first) > 32) {
			parser->error = "URN NID is too long";
			return -1;
		}
		parser->error = p_strdup_printf(parser->pool,
			"URN NID contains invalid character %s",
			uri_char_sanitize(*parser->cur));
		return -1;
	}
	if (!urn_char_is_alphanum(*(parser->cur - 1))) {
		parser->error = p_strdup_printf(parser->pool,
			"URN NID ends with invalid character %s",
			uri_char_sanitize(*(parser->cur - 1)));
		return -1;
	}
	if ((parser->cur - first) < 2) {
		parser->error = "URN NID is too short";
		return -1;
	}

	if (urn != NULL)
		urn->nid = p_strdup_until(parser->pool, first, parser->cur);
	return 0;
}

static int uri_parse_nss(struct urn_parser *urn_parser)
{
	struct uri_parser *parser = &urn_parser->parser;
	struct urn *urn = urn_parser->urn;
	const unsigned char *first = parser->cur;
	string_t *nss = NULL;
	int ret;

	/* NSS = pchar *(pchar / "/")
	 */

	if (parser->cur >= parser->end) {
		parser->error = "URN NSS is empty";
		return -1;
	}

	if (!urn_char_is_pchar(*parser->cur)) {
		parser->error = p_strdup_printf(parser->pool,
			"URN NSS begins with invalid character %s",
			uri_char_sanitize(*parser->cur));
		return -1;
	}
	parser->cur++;

	if (urn != NULL)
		nss = t_str_new(128);

	/* pchar *( pchar / "/" / "?" ) */
	while (parser->cur < parser->end) {
		if (*parser->cur == '%') {
			const unsigned char *pct = parser->cur;
			unsigned char ch = 0;

			ret = uri_parse_pct_encoded(parser, &ch);
			if (ret < 0)
				return -1;
			i_assert(ret > 0);

			if (urn != NULL) {
				str_append_data(nss, first, pct - first);
				if (!urn_parser->normalizing)
					str_append_c(nss, ch);
				else
					str_printfa(nss, "%%%02X", ch);
				first = parser->cur;
			}
			continue;
		}
		if (!urn_char_is_pchar_slash(*parser->cur))
			break;
		parser->cur++;
	}

	if (parser->cur < parser->end &&
	    *parser->cur != '?' && *parser->cur != '#') {
		parser->error = p_strdup_printf(parser->pool,
			"URN NSS contains invalid character %s",
			uri_char_sanitize(*parser->cur));
		return -1;
	}
	if (urn != NULL) {
		str_append_data(nss, first, parser->cur - first);
		urn->nss = p_strdup(parser->pool, str_c(nss));
	}
	return 0;
}

static int urn_parse_assigned_name(struct urn_parser *urn_parser)
{
	struct uri_parser *parser = &urn_parser->parser;
	struct urn *urn = urn_parser->urn;
	const unsigned char *first = parser->cur;

	/* assigned-name = "urn" ":" NID ":" NSS
	   NID           = (alphanum) 0*30(ldh) (alphanum)
	   ldh           = alphanum / "-"
	   NSS           = pchar *(pchar / "/")

	   The "urn:" prefix is already parsed at this point.
	 */

	/* NID */
	if (uri_parse_nid(urn_parser) < 0)
		return -1;

	/* : */
	i_assert(*parser->cur == ':');
	parser->cur++;

	/* NSS */
	if (uri_parse_nss(urn_parser) < 0)
		return -1;

	if (urn != NULL && !urn_parser->normalizing) {
		urn->assigned_name = p_strconcat(parser->pool,
			"urn:", t_strdup_until(first, parser->cur), NULL);
	}
	return 0;
}

static int
urn_parse_rq_component(struct urn_parser *urn_parser, bool query,
		       const char **comp_r)
{
	struct uri_parser *parser = &urn_parser->parser;
	const unsigned char *first = parser->cur;
	int ret;

	/* rq-components = [ "?+" r-component ]
	                 [ "?=" q-component ]
	   r-component   = pchar *( pchar / "/" / "?" )
	   q-component   = pchar *( pchar / "/" / "?" )
	 */

	/* "?" */
	if (parser->cur >= parser->end || *parser->cur != '?')
		return 0;
	parser->cur++;

	/* "+" / "=" */
	if (parser->cur >= parser->end) {
		parser->error = "URN assinged name ends in bare '?'";
		return -1;
	} else if (query && *parser->cur == '+') {
		parser->error = p_strdup_printf(parser->pool,
						"URN has second R component");
		return -1;
	} else if (!query && *parser->cur == '=') {
		parser->cur = first;
		return 0;
	} else if (*parser->cur != '+' && *parser->cur != '=' ) {
		parser->error = p_strdup_printf(parser->pool,
			"URN %sQ component starts with invalid character %s",
			(query ? "" : "R or "),
			uri_char_sanitize(*parser->cur));
		return -1;
	}
	parser->cur++;

	/* pchar *( pchar / "/" / "?" ) */
	while (parser->cur < parser->end) {
		if (*parser->cur == '%') {
			unsigned char ch = 0;

			ret = uri_parse_pct_encoded(parser, &ch);
			if (ret < 0)
				return -1;
			if (ret > 0)
				continue;
		}
		if (*parser->cur == '?' && !query &&
		    parser->cur < parser->end && *(parser->cur + 1) == '=')
			break;
		if (!urn_char_is_component(*parser->cur))
			break;
		parser->cur++;
	}

	if (!parser->parse_prefix && parser->cur < parser->end &&
	    (query || *parser->cur != '?') &&
	    *parser->cur != '#') {
		parser->error = p_strdup_printf(parser->pool,
			"%s component contains invalid character %s",
			(query ? "Q" : "R"),
			uri_char_sanitize(*parser->cur));
		return -1;
	}

	if (comp_r != NULL && !urn_parser->normalizing)
		*comp_r = p_strdup_until(parser->pool, first+2, parser->cur);
	return 1;
}

static int urn_parse_f_component(struct urn_parser *urn_parser)
{
	struct uri_parser *parser = &urn_parser->parser;
	struct urn *urn = urn_parser->urn;
	const char *fragment;
	int ret;

	/* [ "#" f-component ]
	   f-component   = fragment
	 */

	ret = uri_parse_fragment(parser, &fragment);
	if (ret < 0)
		return -1;
	if (urn == NULL)
		return 0;
	if (ret > 0 && !urn_parser->normalizing)
		urn->enc_f_component = p_strdup(parser->pool, fragment);
	return ret;
}

static int urn_do_parse(struct urn_parser *urn_parser)
{
	struct uri_parser *parser = &urn_parser->parser;
	struct urn *urn = urn_parser->urn;
	const char *scheme;
	int ret;

	/* "urn:" */
	ret = urn_parse_scheme(urn_parser, &scheme);
	if (ret < 0) {
		parser->error = "Not a valid URI";
		return -1;
	}
	if (ret > 0) {
		i_assert(scheme != NULL);
		if (strcasecmp(scheme, "urn") != 0) {
			parser->error = "Not an URN";
			return -1;
		}
	}

	/* assigned-name ("urn:" already parsed) */
	if (urn_parse_assigned_name(urn_parser) < 0)
		return -1;

	/* [ "?+" r-component ] */
	if (urn_parse_rq_component(urn_parser, FALSE,
				   (urn == NULL ?
				    NULL : &urn->enc_r_component)) < 0)
		return -1;

	/* [ "?=" q-component ] */
	if (urn_parse_rq_component(urn_parser, TRUE,
				   (urn == NULL ?
				    NULL : &urn->enc_q_component)) < 0)
		return -1;

	/* [ "#" f-component ] */
	if (urn_parse_f_component(urn_parser) < 0)
		return -1;

	/* must be at end of URN now */
	i_assert(parser->cur == parser->end);

	return 0;
}

int urn_parse(const char *urn, enum urn_parse_flags flags, pool_t pool,
	      struct urn **urn_r, const char **error_r)
{
	struct urn_parser urn_parser;

	*error_r = NULL;

	i_assert(urn_r == NULL || pool != NULL);

	i_zero(&urn_parser);
	uri_parser_init(&urn_parser.parser, pool, urn);

	urn_parser.urn = (urn_r == NULL ? NULL : p_new(pool, struct urn, 1));
	urn_parser.flags = flags;

	if (urn_do_parse(&urn_parser) < 0) {
		*error_r = urn_parser.parser.error;
		return -1;
	}
	if (urn_r != NULL)
		*urn_r = urn_parser.urn;
	return 0;
}

int urn_validate(const char *urn, enum urn_parse_flags flags,
		 const char **error_r)
{
	return urn_parse(urn, flags, NULL, NULL, error_r);
}

/*
 * URN construction
 */

const char *urn_create(const struct urn *urn)
{
	string_t *urnstr = t_str_new(512);

	uri_append_scheme(urnstr, "urn");
	if (urn->nid != NULL) {
		i_assert(urn->nss != NULL);
		str_append(urnstr, urn->nid);
		str_append_c(urnstr, ':');
		i_assert(*urn->nss != '/');
		uri_data_encode(urnstr, urn_char_lookup,
				urn_pchar_slash_char_mask, "", urn->nss);
	} else {
		const char *suffix, *nid_end, *nss;

		i_assert(urn->assigned_name != NULL);
		if (!str_begins_icase(urn->assigned_name, "urn:", &suffix))
			i_unreached();
		nid_end = strchr(suffix, ':');
		i_assert(nid_end != NULL);
		nss = nid_end + 1;
		i_assert(*nss != '/');
		str_append(urnstr, suffix);
	}

	/* r-component (pre-encoded) */
	if (urn->enc_r_component != NULL) {
		str_append(urnstr, "?+");
		str_append(urnstr, urn->enc_r_component);
	}
	/* q-component (pre-encoded) */
	if (urn->enc_q_component != NULL) {
		str_append(urnstr, "?=");
		str_append(urnstr, urn->enc_q_component);
	}

	/* fragment (pre-encoded) */
	if (urn->enc_f_component != NULL) {
		str_append_c(urnstr, '#');
		str_append(urnstr, urn->enc_f_component);
	}

	return str_c(urnstr);
}

/*
 * URN equality
 */

int urn_normalize(const char *urn_in, enum urn_parse_flags flags,
		  const char **urn_out_r, const char **error_r)
{
	struct urn_parser urn_parser;
	struct urn urn;

	*error_r = NULL;

	i_zero(&urn_parser);
	uri_parser_init(&urn_parser.parser, pool_datastack_create(), urn_in);

	urn_parser.urn = &urn;
	urn_parser.flags = flags;
	urn_parser.normalizing = TRUE;

	if (urn_do_parse(&urn_parser) < 0) {
		*error_r = urn_parser.parser.error;
		return -1;
	}

	string_t *urnstr = t_str_new(512);

	if ((flags & URN_PARSE_SCHEME_EXTERNAL) == 0)
		uri_append_scheme(urnstr, "urn");

	i_assert(urn.nss != NULL);
	str_append(urnstr, t_str_lcase(urn.nid));
	str_append_c(urnstr, ':');
	i_assert(*urn.nss != '/');
	str_append(urnstr, urn.nss);

	*urn_out_r = str_c(urnstr);
	return 0;
}

int urn_equals(const char *urn1, const char *urn2, enum urn_parse_flags flags,
	       const char **error_r)
{
	const char *urn1n, *urn2n;

	if (urn_normalize(urn1, flags, &urn1n, error_r) < 0)
		return -1;
	if (urn_normalize(urn2, flags, &urn2n, error_r) < 0)
		return -1;

	if (strcmp(urn1n, urn2n) == 0)
		return 1;
	return 0;
}
