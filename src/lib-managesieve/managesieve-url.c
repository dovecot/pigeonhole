/* Copyright (c) 2021 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "strfuncs.h"
#include "net.h"
#include "uri-util.h"

#include "managesieve-url.h"

/* RFC 5804, Section 3:

   sieveurl = sieveurl-server / sieveurl-list-scripts /
              sieveurl-script

   sieveurl-server = "sieve://" authority

   sieveurl-list-scripts = "sieve://" authority ["/"]

   sieveurl-script = "sieve://" authority "/"
                     [owner "/"] scriptname

   authority = <defined in [URI-GEN]>

   owner         = *ochar
                   ;; %-encoded version of [SASL] authorization
                   ;; identity (script owner) or "userid".
                   ;;
                   ;; Empty owner is used to reference
                   ;; global scripts.
                   ;;
                   ;; Note that ASCII characters such as " ", ";",
                   ;; "&", "=", "/" and "?" must be %-encoded
                   ;; as per rule specified in [URI-GEN].

   scriptname    = 1*ochar
                   ;; %-encoded version of UTF-8 representation
                   ;; of the script name.
                   ;; Note that ASCII characters such as " ", ";",
                   ;; "&", "=", "/" and "?" must be %-encoded
                   ;; as per rule specified in [URI-GEN].

   ochar         = unreserved / pct-encoded / sub-delims-sh /
                   ":" / "@"
                   ;; Same as [URI-GEN] 'pchar',
                   ;; but without ";", "&" and "=".

   unreserved = <defined in [URI-GEN]>

   pct-encoded = <defined in [URI-GEN]>

   sub-delims-sh = "!" / "$" / "'" / "(" / ")" /
                   "*" / "+" / ","
                   ;; Same as [URI-GEN] sub-delims,
                   ;; but without ";", "&" and "=".
 */

/* Character lookup table

   unreserved    = ALPHA / DIGIT / "-" / "." / "_" / "~"     [bit0]
   sub-delims-sh = "!" / "$" / "'" / "(" / ")" /
                   "*" / "+" / ","
                   ;; Same as [URI-GEN] sub-delims,
                   ;; but without ";", "&" and "=".          [bit1]
   ochar         = unreserved / pct-encoded / sub-delims-sh /
                   ":" / "@"                                 [bit0|bit1|bit2]
 */

static const unsigned char managesieve_url_ochar_mask = (1<<0)|(1<<1)|(1<<2);

static const unsigned char managesieve_url_char_lookup[256] = {
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 00
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 10
	 2,  0,  0,  0,  2,  0,  0,  2,  2,  2,  2,  2,  2,  1,  1,  0,  // 20
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  4,  0,  0,  0,  0,  0,  // 30
	 4,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 40
	 1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  0,  1,  // 50
	 0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 60
	 1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  1,  0,  // 70

	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 80
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 90
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // a0
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // b0
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // c0
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // d0
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // e0
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // f0
};

/*
 * Sieve URL parser
 */

struct managesieve_url_parser {
	struct uri_parser parser;

	enum managesieve_url_parse_flags flags;

	struct managesieve_url *url;
	struct managesieve_url *base;
};

static int
managesieve_url_parse_scheme(struct managesieve_url_parser *url_parser)
{
	struct uri_parser *parser = &url_parser->parser;
	const char *scheme;
	int ret;

	if ((url_parser->flags & MANAGESIEVE_URL_PARSE_SCHEME_EXTERNAL) != 0)
		return 1;

	ret = uri_parse_scheme(parser, &scheme);
	if (ret < 0)
		return -1;
	if (ret == 0) {
		parser->error = "Relative Sieve URL not allowed";
		return -1;
	}

	if (strcasecmp(scheme, "sieve") != 0) {
		parser->error = "Not a Sieve URL";
		return -1;
	}
	return 0;
}

static int
managesieve_url_parse_userinfo(struct managesieve_url_parser *url_parser,
			       struct uri_authority *auth,
			       const char **user_r, const char **password_r)
{
	struct uri_parser *parser = &url_parser->parser;
	const char *p;

	*user_r = *password_r = NULL;

	if (auth->enc_userinfo == NULL)
		return 0;
	if ((url_parser->flags & MANAGESIEVE_URL_ALLOW_USERINFO_PART) == 0) {
		parser->error = "Sieve URL does not allow `userinfo@' part";
		return -1;
	}

	p = strchr(auth->enc_userinfo, ':');
	if (p == NULL) {
		if (!uri_data_decode(parser, auth->enc_userinfo, NULL, user_r))
			return -1;
	} else {
		if (!uri_data_decode(parser, auth->enc_userinfo, p, user_r))
			return -1;
		if (!uri_data_decode(parser, p + 1, NULL, password_r))
			return -1;
	}
	return 0;
}

static int
managesieve_url_parse_authority(struct managesieve_url_parser *url_parser)
{
	struct uri_parser *parser = &url_parser->parser;
	struct managesieve_url *url = url_parser->url;
	struct uri_authority auth;
	const char *user = NULL, *password = NULL;
	int ret;

	if ((ret = uri_parse_host_authority(parser, &auth)) < 0)
		return -1;
	if (auth.host.name == NULL || *auth.host.name == '\0') {
		parser->error =
			"Sieve URL does not allow empty host identifier";
		return -1;
	}
	if (ret > 0) {
		if (managesieve_url_parse_userinfo(url_parser, &auth,
						   &user, &password) < 0)
			return -1;
	}
	if (url != NULL) {
		uri_host_copy(parser->pool, &url->host, &auth.host);
		url->port = auth.port;
		url->user = p_strdup(parser->pool, user);
		url->password = p_strdup(parser->pool, password);
	}
	return 0;
}

static int
managesieve_url_parse_path_segment(struct managesieve_url_parser *url_parser,
				   const char **segment_r) ATTR_NULL(2)
{
	struct uri_parser *parser = &url_parser->parser;
	const unsigned char *first, *offset;
	string_t *segment = NULL;
	int ret;

	first = offset = parser->cur;
	if (segment_r != NULL)
		segment = t_str_new(128);
	while (parser->cur < parser->end) {
		if (*parser->cur == '%') {
			unsigned char ch = 0;

			if (segment != NULL) {
				str_append_data(segment, offset,
						parser->cur - offset);
			}

			ret = uri_parse_pct_encoded(parser, &ch);
			if (ret < 0)
				return -1;
			i_assert(ret > 0);

			if (segment != NULL)
				str_append_c(segment, ch);
			offset = parser->cur;
			continue;
		}
		if ((managesieve_url_char_lookup[*parser->cur] &
		     managesieve_url_ochar_mask) == 0)
			break;
		parser->cur++;
	}
	if (segment != NULL)
		str_append_data(segment, offset, parser->cur - offset);

	if (parser->cur < parser->end && *parser->cur != '/' &&
	    *parser->cur != '?' && *parser->cur != '#') {
		parser->error = p_strdup_printf(parser->pool,
			"Path segment contains invalid character %s",
			uri_char_sanitize(*parser->cur));
		return -1;
	}

	if (first == parser->cur)
		return 0;

	if (segment != NULL)
		*segment_r = p_strdup(parser->pool, str_c(segment));
	return 1;
}

static int
managesieve_url_parse_path(struct managesieve_url_parser *url_parser)
{
	struct uri_parser *parser = &url_parser->parser;
	struct managesieve_url *url = url_parser->url;
	const char *segment1, *segment2;
	int ret;

	if (parser->cur >= parser->end || *parser->cur != '/')
		return 0;
	parser->cur++;

	ret = managesieve_url_parse_path_segment(url_parser,
						 (url == NULL ?
						  NULL : &segment1));
	if (ret < 0)
		return -1;
	if (ret == 0) {
		if (url != NULL)
			url->scriptname = "";
		return 1;
	}

	if (parser->cur >= parser->end || *parser->cur != '/') {
		if (url != NULL)
			url->scriptname = segment1;
		return 1;
	}
	parser->cur++;

	ret = managesieve_url_parse_path_segment(url_parser,
						 (url == NULL ?
						  NULL : &segment2));
	if (ret < 0)
		return -1;
	if (ret == 0) {
		parser->error = "Empty script name";
		return -1;
	}
	if (*parser->cur == '/') {
		parser->error = "Script name contains invalid character '/'";
		return -1;
	}

	if (url != NULL) {
		url->owner = segment1;
		url->scriptname = segment2;
	}
	return 1;
}

static int managesieve_url_do_parse(struct managesieve_url_parser *url_parser)
{
	struct uri_parser *parser = &url_parser->parser;

	/* "sieve:" */
	if (managesieve_url_parse_scheme(url_parser) < 0)
		return -1;

	/* "//" authority
	 */
	if (parser->cur >= parser->end || parser->cur[0] != '/' ||
	    (parser->cur + 1) >= parser->end || parser->cur[1] != '/') {
		parser->error = "Sieve URL requires `//' after `sieve:'";
		return -1;
	}
	parser->cur += 2;

	if (managesieve_url_parse_authority(url_parser) < 0)
		return -1;

	/*  "/" [owner "/"] scriptname */
	if (managesieve_url_parse_path(url_parser) < 0)
		return -1;

	/* Sieve URL has no query */
	if (*parser->cur == '?') {
		parser->error = "Query component not allowed in Sieve URL";
		return -1;
	}

	/* Sieve URL has no fragment */
	if (*parser->cur == '#') {
		parser->error = "Fragment component not allowed in Sieve URL";
		return -1;
	}

	/* Must be at end of URL now */
	i_assert(parser->cur == parser->end);

	return 0;
}

/* Public API */

int managesieve_url_parse(const char *url,
			  enum managesieve_url_parse_flags flags, pool_t pool,
			  struct managesieve_url **url_r, const char **error_r)
{
	struct managesieve_url_parser url_parser;

	i_zero(&url_parser);
	uri_parser_init(&url_parser.parser, pool, url);

	if (url_r != NULL)
		url_parser.url = p_new(pool, struct managesieve_url, 1);
	url_parser.flags = flags;

	if (managesieve_url_do_parse(&url_parser) < 0) {
		i_assert(url_parser.parser.error != NULL);
		*error_r = url_parser.parser.error;
		return -1;
	}

	if (url_r != NULL)
		*url_r = url_parser.url;
	return 0;
}

/*
 * Sieve URL manipulation
 */

void managesieve_url_init_authority_from(struct managesieve_url *dest,
					 const struct managesieve_url *src)
{
	i_zero(dest);
	dest->host = src->host;
	dest->port = src->port;
}

void managesieve_url_copy_authority(pool_t pool, struct managesieve_url *dest,
				    const struct managesieve_url *src)
{
	i_zero(dest);
	uri_host_copy(pool, &dest->host, &src->host);
	dest->port = src->port;
}

struct managesieve_url *
managesieve_url_clone_authority(pool_t pool, const struct managesieve_url *src)
{
	struct managesieve_url *new_url;

	new_url = p_new(pool, struct managesieve_url, 1);
	managesieve_url_copy_authority(pool, new_url, src);

	return new_url;
}

void managesieve_url_copy(pool_t pool, struct managesieve_url *dest,
			  const struct managesieve_url *src)
{
	managesieve_url_copy_authority(pool, dest, src);
	dest->owner = p_strdup(pool, src->owner);
	dest->scriptname = p_strdup(pool, src->scriptname);
}

void managesieve_url_copy_with_userinfo(pool_t pool,
					struct managesieve_url *dest,
					const struct managesieve_url *src)
{
	managesieve_url_copy(pool, dest, src);
	dest->user = p_strdup(pool, src->user);
	dest->password = p_strdup(pool, src->password);
}

struct managesieve_url *
managesieve_url_clone(pool_t pool, const struct managesieve_url *src)
{
	struct managesieve_url *new_url;

	new_url = p_new(pool, struct managesieve_url, 1);
	managesieve_url_copy(pool, new_url, src);

	return new_url;
}

struct managesieve_url *
managesieve_url_clone_with_userinfo(pool_t pool,
				    const struct managesieve_url *src)
{
	struct managesieve_url *new_url;

	new_url = p_new(pool, struct managesieve_url, 1);
	managesieve_url_copy_with_userinfo(pool, new_url, src);

	return new_url;
}

/*
 * Sieve URL construction
 */

static void
managesieve_url_add_scheme(string_t *urlstr)
{
	/* scheme */
	uri_append_scheme(urlstr, "sieve");
	str_append(urlstr, "//");
}

static void
managesieve_url_add_authority(string_t *urlstr,
			      const struct managesieve_url *url)
{
	/* userinfo */
	if (url->user != NULL) {
		if (url->user != NULL)
			uri_append_user_data(urlstr, ";:", url->user);
		str_append_c(urlstr, '@');
	}
	/* host */
	uri_append_host(urlstr, &url->host);
	/* port */
	if (url->port != MANAGESIEVE_DEFAULT_PORT)
		uri_append_port(urlstr, url->port);
}

static void
managesieve_url_add_path(string_t *urlstr, const struct managesieve_url *url)
{
	if (url->owner == NULL) {
		if (url->scriptname == NULL)
			return;
	} else {
		i_assert(url->scriptname != NULL && *url->scriptname != '\0');

		str_append_c(urlstr, '/');
		uri_append_path_segment_data(urlstr, ";&=", url->owner);
	}

	str_append_c(urlstr, '/');
	uri_append_path_segment_data(urlstr, ";&=", url->scriptname);
}

const char *managesieve_url_create(const struct managesieve_url *url)
{
	string_t *urlstr = t_str_new(512);

	managesieve_url_add_scheme(urlstr);
	managesieve_url_add_authority(urlstr, url);
	managesieve_url_add_path(urlstr, url);

	return str_c(urlstr);
}

const char *managesieve_url_create_host(const struct managesieve_url *url)
{
	string_t *urlstr = t_str_new(512);

	managesieve_url_add_scheme(urlstr);
	managesieve_url_add_authority(urlstr, url);

	return str_c(urlstr);
}

const char *managesieve_url_create_authority(const struct managesieve_url *url)
{
	string_t *urlstr = t_str_new(256);

	managesieve_url_add_authority(urlstr, url);

	return str_c(urlstr);
}
