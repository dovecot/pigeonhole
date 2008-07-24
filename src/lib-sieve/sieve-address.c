#include "lib.h"
#include "str.h"
#include "rfc822-parser.h"

#include "sieve-common.h"

#include "sieve-address.h"

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
 *                           / phrase "<" addr-spec ">" ; name & addr-spec
 */ 
 
struct sieve_address_parser_context {
	struct rfc822_parser_context parser;

	string_t *address;

	string_t *str;
	string_t *local_part;
	string_t *domain;
	
	string_t *error;
};

static inline void sieve_address_error
	(struct sieve_address_parser_context *ctx, const char *fmt, ...) 
		ATTR_FORMAT(2, 3);

static inline void sieve_address_error
	(struct sieve_address_parser_context *ctx, const char *fmt, ...)
{
	va_list args;
	
	if ( str_len(ctx->error) == 0 ) {
		va_start(args, fmt);
		str_vprintfa(ctx->error, fmt, args);
		va_end(args);
	}
}
	
static int parse_local_part(struct sieve_address_parser_context *ctx)
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
	if (*ctx->parser.data == '"')
		ret = rfc822_parse_quoted_string(&ctx->parser, ctx->local_part);
	else
		ret = rfc822_parse_dot_atom(&ctx->parser, ctx->local_part);
		
	if (ret < 0) {
		sieve_address_error(ctx, "invalid local part");
		return -1;
	}

	return ret;
}

static int parse_domain(struct sieve_address_parser_context *ctx)
{
	int ret;

	str_truncate(ctx->domain, 0);
	if ((ret = rfc822_parse_domain(&ctx->parser, ctx->domain)) < 0) {
		sieve_address_error(ctx, "invalid or missing domain");
		return -1;
	}

	return ret;
}

static int parse_addr_spec(struct sieve_address_parser_context *ctx)
{
	/* addr-spec       = local-part "@" domain */
	int ret;

	if ((ret = parse_local_part(ctx)) < 0)
		return ret;
	
	if ( ret > 0 && *ctx->parser.data == '@') {
		return parse_domain(ctx);
	} 

	sieve_address_error(ctx, "invalid or lonely local part '%s' (expecting '@')", 
		str_c(ctx->local_part));
	return -1;
}

static int parse_name_addr(struct sieve_address_parser_context *ctx)
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

	if ((ret = parse_addr_spec(ctx)) < 0)
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

static bool parse_sieve_address(struct sieve_address_parser_context *ctx)
{
	int ret;
	
	/* Initialize parser */
	
	rfc822_parser_init(&ctx->parser, str_data(ctx->address), str_len(ctx->address), 
		t_str_new(128));

	/* Parse */
	
	rfc822_skip_lwsp(&ctx->parser);

	if (ctx->parser.data == ctx->parser.end) {
		sieve_address_error(ctx, "empty address");
		return FALSE;
	}
	
	if ((ret = parse_name_addr(ctx)) < 0) {
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

const char *sieve_address_normalize
(string_t *address, const char **error_r)
{
	struct sieve_address_parser_context ctx;

	memset(&ctx, 0, sizeof(ctx));
	ctx.address = address;
	
	ctx.local_part = t_str_new(128);
	ctx.domain = t_str_new(128);
	ctx.str = t_str_new(128);
	ctx.error = t_str_new(128);

	if ( !parse_sieve_address(&ctx) )
	{
		*error_r = str_c(ctx.error);
		return NULL;
	}
	
	*error_r = NULL;
	(void)str_lcase(str_c_modifiable(ctx.domain));
	
	return t_strconcat(str_c(ctx.local_part), "@", str_c(ctx.domain), NULL);
}

bool sieve_address_validate
(string_t *address, const char **error_r)
{
	struct sieve_address_parser_context ctx;

	memset(&ctx, 0, sizeof(ctx));
	ctx.address = address;

	ctx.local_part = ctx.domain = ctx.str = t_str_new(128);
	ctx.error = t_str_new(128);

	if ( !parse_sieve_address(&ctx) )
	{
		*error_r = str_c(ctx.error);
		return FALSE;
	}
	
	*error_r = NULL;
	return TRUE;
}



