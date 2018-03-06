/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "str-sanitize.h"
#include "rfc822-parser.h"
#include "message-address.h"

#include "sieve-common.h"
#include "sieve-runtime-trace.h"

#include "sieve-address.h"

#include <ctype.h>

/*
 * Header address list
 */

/* Forward declarations */

static int sieve_header_address_list_next_string_item
	(struct sieve_stringlist *_strlist, string_t **str_r);
static int sieve_header_address_list_next_item
	(struct sieve_address_list *_addrlist, struct smtp_address *addr_r,
		string_t **unparsed_r);
static void sieve_header_address_list_reset
	(struct sieve_stringlist *_strlist);
static void sieve_header_address_list_set_trace
	(struct sieve_stringlist *_strlist, bool trace);

/* Stringlist object */

struct sieve_header_address_list {
	struct sieve_address_list addrlist;

	struct sieve_stringlist *field_values;
	const struct message_address *cur_address;
};

struct sieve_address_list *sieve_header_address_list_create
(const struct sieve_runtime_env *renv, struct sieve_stringlist *field_values)
{
	struct sieve_header_address_list *addrlist;

	addrlist = t_new(struct sieve_header_address_list, 1);
	addrlist->addrlist.strlist.runenv = renv;
	addrlist->addrlist.strlist.exec_status = SIEVE_EXEC_OK;
	addrlist->addrlist.strlist.next_item =
		sieve_header_address_list_next_string_item;
	addrlist->addrlist.strlist.reset = sieve_header_address_list_reset;
	addrlist->addrlist.strlist.set_trace = sieve_header_address_list_set_trace;
	addrlist->addrlist.next_item = sieve_header_address_list_next_item;
	addrlist->field_values = field_values;

	return &addrlist->addrlist;
}

static int sieve_header_address_list_next_item
(struct sieve_address_list *_addrlist, struct smtp_address *addr_r,
	string_t **unparsed_r)
{
	struct sieve_header_address_list *addrlist =
		(struct sieve_header_address_list *) _addrlist;
	const struct message_address *aitem;
	bool valid = TRUE;

	if ( addr_r != NULL )
		smtp_address_init(addr_r, NULL, NULL);
	if ( unparsed_r != NULL ) *unparsed_r = NULL;

	/* Parse next header field value if necessary */
	while ( addrlist->cur_address == NULL ) {
		string_t *value_item = NULL;
		int ret;

		/* Read next header value from source list */
		if ( (ret=sieve_stringlist_next_item(addrlist->field_values, &value_item))
			<= 0 )
			return ret;

		if ( _addrlist->strlist.trace ) {
			sieve_runtime_trace(_addrlist->strlist.runenv, 0,
				"parsing address header value `%s'",
				str_sanitize(str_c(value_item), 80));
		}

		addrlist->cur_address = message_address_parse
			(pool_datastack_create(), (const unsigned char *) str_data(value_item),
				str_len(value_item), 256, FALSE);

		/* Check validity of all addresses simultaneously. Unfortunately,
		 * errorneous addresses cannot be extracted from the address list.
		 */
		aitem = addrlist->cur_address;
		while ( aitem != NULL) {
			if ( aitem->invalid_syntax )
				valid = FALSE;
			aitem = aitem->next;
		}

		if ( addrlist->cur_address == NULL || !valid ) {
			addrlist->cur_address = NULL;

			if ( unparsed_r != NULL) *unparsed_r = value_item;
			return 1;
		}

		/* Find first usable address */
		aitem = addrlist->cur_address;
		while ( aitem != NULL && aitem->domain == NULL ) {
			aitem = aitem->next;
		}

		addrlist->cur_address = aitem;
	}

	/* Return next item */

	if ( addr_r != NULL ) {
		smtp_address_init(addr_r,
			addrlist->cur_address->mailbox,
			addrlist->cur_address->domain);
	}

	/* Find next usable address */
	aitem = addrlist->cur_address->next;
	while ( aitem != NULL && aitem->domain == NULL ) {
		aitem = aitem->next;
	}
	addrlist->cur_address = aitem;

	return 1;
}

static int sieve_header_address_list_next_string_item
(struct sieve_stringlist *_strlist, string_t **str_r)
{
	struct sieve_address_list *addrlist = (struct sieve_address_list *)_strlist;
	struct smtp_address addr;
	int ret;

	if ( (ret=sieve_header_address_list_next_item
		(addrlist, &addr, str_r)) <= 0 )
		return ret;

	if ( addr.localpart != NULL ) {
		const char *addr_str =	smtp_address_encode(&addr);
		if ( str_r != NULL )
			*str_r = t_str_new_const(addr_str, strlen(addr_str));
	}

	return 1;
}

static void sieve_header_address_list_reset
(struct sieve_stringlist *_strlist)
{
	struct sieve_header_address_list *addrlist =
		(struct sieve_header_address_list *)_strlist;

	sieve_stringlist_reset(addrlist->field_values);
	addrlist->cur_address = NULL;
}

static void sieve_header_address_list_set_trace
(struct sieve_stringlist *_strlist, bool trace)
{
	struct sieve_header_address_list *addrlist =
		(struct sieve_header_address_list *)_strlist;

	sieve_stringlist_set_trace(addrlist->field_values, trace);
}

/*
 * RFC 2822 addresses
 */

/* Mail message address according to RFC 2822 and implemented in the Dovecot
 * message address parser:
 *
 *   address         =       mailbox / group
 *   mailbox         =       name-addr / addr-spec
 *   name-addr       =       [display-name] angle-addr
 *   angle-addr      =       [CFWS] "<" addr-spec ">" [CFWS] / obs-angle-addr
 *   group           =       display-name ":" [mailbox-list / CFWS] ";" [CFWS]
 *   display-name    =       phrase
 *
 *   addr-spec       =       local-part "@" domain
 *   local-part      =       dot-atom / quoted-string / obs-local-part
 *   domain          =       dot-atom / domain-literal / obs-domain
 *   domain-literal  =       [CFWS] "[" *([FWS] dcontent) [FWS] "]" [CFWS]
 *   dcontent        =       dtext / quoted-pair
 *   dtext           =       NO-WS-CTL /     ; Non white space controls
 *                           %d33-90 /       ; The rest of the US-ASCII
 *                           %d94-126        ;  characters not including "[",
 *                                           ;  "]", or "\"
 *
 *   atext           =       ALPHA / DIGIT / ; Any character except controls,
 *                           "!" / "#" /     ;  SP, and specials.
 *                           "$" / "%" /     ;  Used for atoms
 *                           "&" / "'" /
 *                           "*" / "+" /
 *                           "-" / "/" /
 *                           "=" / "?" /
 *                           "^" / "_" /
 *                           "`" / "{" /
 *                           "|" / "}" /
 *                           "~"
 *   atom            =       [CFWS] 1*atext [CFWS]
 *   dot-atom        =       [CFWS] dot-atom-text [CFWS]
 *   dot-atom-text   =       1*atext *("." 1*atext)
 *   word            =       atom / quoted-string
 *   phrase          =       1*word / obs-phrase
 *
 * Message address specification as allowed bij the RFC 5228 SIEVE
 * specification:
 *   sieve-address   =       addr-spec                  ; simple address
 *                           / phrase "<" addr-spec ">" ; name & addr-spec\
 *
 * Which concisely is about equal to:
 *   sieve-address   =       mailbox
 */

/*
 * Address parse context
 */

struct sieve_message_address_parser {
	struct rfc822_parser_context parser;

	string_t *str;
	string_t *local_part;
	string_t *domain;

	string_t *error;
};

/*
 * Error handling
 */

static inline void sieve_address_error
	(struct sieve_message_address_parser *ctx, const char *fmt, ...)
		ATTR_FORMAT(2, 3);

static inline void sieve_address_error
	(struct sieve_message_address_parser *ctx, const char *fmt, ...)
{
	va_list args;

	if ( str_len(ctx->error) == 0 ) {
		va_start(args, fmt);
		str_vprintfa(ctx->error, fmt, args);
		va_end(args);
	}
}

/*
 * Partial RFC 2822 address parser
 *
 *   FIXME: lots of overlap with dovecot/src/lib-mail/message-parser.c
 *          --> this implementation adds textual error reporting
 *          MERGE!
 */

static int check_local_part(struct sieve_message_address_parser *ctx)
{
	const unsigned char *p, *pend;

	p = str_data(ctx->local_part);
	pend = p + str_len(ctx->local_part);
	while (p < pend) {
		if (*p < 0x20 || *p > 0x7e)
			return -1;
		p++;
	}
	return 0;
}

static int parse_local_part(struct sieve_message_address_parser *ctx)
{
	int ret;

	/*
	   local-part      = dot-atom / quoted-string / obs-local-part
	   obs-local-part  = word *("." word)
	*/
	if (ctx->parser.data == ctx->parser.end) {
		sieve_address_error(ctx, "empty local part");
		return -1;
	}

	str_truncate(ctx->local_part, 0);
	if (*ctx->parser.data == '"') {
		ret = rfc822_parse_quoted_string(&ctx->parser, ctx->local_part);
	} else {
		ret = -1;
		/* NOTE: this deviates from dot-atom syntax to allow some Japanese
		   mail addresses with dots at non-standard places to be accepted. */
		do {
			while (*ctx->parser.data == '.') {
				str_append_c(ctx->local_part, '.');
				ctx->parser.data++;
				if (ctx->parser.data == ctx->parser.end) {
					/* @domain is missing, but local-part
					   parsing was successful */
					return 0;
				}
				ret = 1;
			}
			if (*ctx->parser.data == '@')
				break;
			ret = rfc822_parse_atom(&ctx->parser, ctx->local_part);
		} while (ret > 0 && *ctx->parser.data == '.');
	}

	if (ret < 0 || check_local_part(ctx) < 0) {
		sieve_address_error(ctx, "invalid local part");
		return -1;
	}

	return ret;
}

static int parse_domain(struct sieve_message_address_parser *ctx)
{
	int ret;

	str_truncate(ctx->domain, 0);
	if ((ret = rfc822_parse_domain(&ctx->parser, ctx->domain)) < 0) {
		sieve_address_error(ctx, "invalid or missing domain");
		return -1;
	}

	return ret;
}

static int parse_addr_spec(struct sieve_message_address_parser *ctx)
{
	/* addr-spec       = local-part "@" domain */
	int ret;

	if ((ret = parse_local_part(ctx)) < 0)
		return ret;

	if ( ret > 0 && *ctx->parser.data == '@') {
		return parse_domain(ctx);
	}

	sieve_address_error(ctx, "invalid or lonely local part '%s' (expecting '@')",
		str_sanitize(str_c(ctx->local_part), 80));
	return -1;
}

static int parse_mailbox(struct sieve_message_address_parser *ctx)
{
	int ret;
	const unsigned char *start;

	/* sieve-address   =       addr-spec                  ; simple address
	 *                         / phrase "<" addr-spec ">" ; name & addr-spec
	 */

	/* Record parser state in case we fail at our first attempt */
	start = ctx->parser.data;

	/* First try: phrase "<" addr-spec ">" ; name & addr-spec */
	str_truncate(ctx->str, 0);
	if (rfc822_parse_phrase(&ctx->parser, ctx->str) <= 0 ||
	    *ctx->parser.data != '<') {
	  /* Failed; try just bare addr-spec */
	  ctx->parser.data = start;
	  return parse_addr_spec(ctx);
	}

	/* "<" addr-spec ">" */
	ctx->parser.data++;

	if ((ret = rfc822_skip_lwsp(&ctx->parser)) <= 0 ) {
		if ( ret < 0 )
			sieve_address_error(ctx, "invalid characters after <");
		return ret;
	}

	if (parse_addr_spec(ctx) < 0)
		return -1;

	if (*ctx->parser.data != '>') {
		sieve_address_error(ctx, "missing '>'");
		return -1;
	}
	ctx->parser.data++;

	if ( (ret=rfc822_skip_lwsp(&ctx->parser)) < 0 )
		sieve_address_error(ctx, "address ends with invalid characters");

	return ret;
}

static bool parse_mailbox_address
(struct sieve_message_address_parser *ctx, const unsigned char *address,
	unsigned int addr_size)
{
	/* Initialize parser */

	rfc822_parser_init(&ctx->parser, address, addr_size, NULL);

	/* Parse */

	rfc822_skip_lwsp(&ctx->parser);

	if (ctx->parser.data == ctx->parser.end) {
		sieve_address_error(ctx, "empty address");
		return FALSE;
	}

	if (parse_mailbox(ctx) < 0)
		return FALSE;

	if (ctx->parser.data != ctx->parser.end) {
		if ( *ctx->parser.data == ',' )
			sieve_address_error(ctx, "not a single addres (found ',')");
		else
			sieve_address_error(ctx, "address ends in invalid characters");
		return FALSE;
	}

	if ( str_len(ctx->domain) == 0 ) {
		/* Not gonna happen */
		sieve_address_error(ctx, "missing domain");
		return FALSE;
	}

	if ( str_len(ctx->local_part) == 0 ) {
		sieve_address_error(ctx, "missing local part");
		return FALSE;
	}

	return TRUE;
}

static bool sieve_address_do_validate
(const unsigned char *address, size_t size,
	const char **error_r)
{
	struct sieve_message_address_parser ctx;

	if ( address == NULL ) {
		*error_r = "null address";
		return FALSE;
	}

	i_zero(&ctx);

	ctx.local_part = t_str_new(128);
	ctx.domain = t_str_new(128);
	ctx.str = t_str_new(128);
	ctx.error = t_str_new(128);

	if ( !parse_mailbox_address(&ctx, address, size) ) {
		if ( error_r != NULL )
			*error_r = str_c(ctx.error);
		return FALSE;
	}

	if ( error_r != NULL )
		*error_r = NULL;

	return TRUE;
}

static const struct smtp_address *sieve_address_do_parse
(const unsigned char *address, size_t size,
	const char **error_r)
{
	struct sieve_message_address_parser ctx;

	if ( error_r != NULL )
		*error_r = NULL;

	if ( address == NULL ) return NULL;

	i_zero(&ctx);

	ctx.local_part = t_str_new(128);
	ctx.domain = t_str_new(128);
	ctx.str = t_str_new(128);
	ctx.error = t_str_new(128);

	if ( !parse_mailbox_address(&ctx, address, size) ) {
		if ( error_r != NULL )
			*error_r = str_c(ctx.error);
		return NULL;
	}

	(void)str_lcase(str_c_modifiable(ctx.domain));

	return smtp_address_create_temp(str_c(ctx.local_part), str_c(ctx.domain));
}

/*
 * Sieve address
 */

const struct smtp_address *sieve_address_parse
(const char *address, const char **error_r)
{
	return sieve_address_do_parse
		((const unsigned char *)address, strlen(address), error_r);
}

const struct smtp_address *sieve_address_parse_str
(string_t *address, const char **error_r)
{
	return sieve_address_do_parse
		(str_data(address), str_len(address), error_r);
}

bool sieve_address_validate
(const char *address, const char **error_r)
{
	return sieve_address_do_validate
		((const unsigned char *)address, strlen(address), error_r);
}

bool sieve_address_validate_str
(string_t *address, const char **error_r)
{
	return sieve_address_do_validate
		(str_data(address), str_len(address), error_r);
}
