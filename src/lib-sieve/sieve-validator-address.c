#include "lib.h"
#include "str.h"
#include "rfc822-parser.h"

#include "sieve-common.h"
#include "sieve-validator.h"

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
	struct sieve_validator *valdtr;
	struct sieve_ast_node *node;
	
	struct rfc822_parser_context parser;

	string_t *address;

	string_t *str;
};

static inline void sieve_address_error
	(struct sieve_address_parser_context *ctx, const char *fmt, ...) 
		ATTR_FORMAT(2, 3);

static inline void sieve_address_error
	(struct sieve_address_parser_context *ctx, const char *fmt, ...)
{
	va_list args;
	
	va_start(args, fmt);
	sieve_validator_error(ctx->valdtr, ctx->node, 
		"specified address '%s' is invalid: %s", str_c(ctx->address), 
		t_strdup_vprintf(fmt, args));
	va_end(args);
}
	
static bool parse_local_part(struct sieve_address_parser_context *ctx)
{
	int ret;

	/*
	   local-part      = dot-atom / quoted-string / obs-local-part
	   obs-local-part  = word *("." word)
	*/
	str_truncate(ctx->str, 0);

	if (ctx->parser.data < ctx->parser.end) {
		if (*ctx->parser.data == '"')
			ret = rfc822_parse_quoted_string(&ctx->parser, ctx->str);
		else
			ret = rfc822_parse_dot_atom(&ctx->parser, ctx->str);
	
		if (ret < 0) {
			sieve_address_error(ctx, "invalid local part");
			return FALSE;
		}
	}

	if ( str_len(ctx->str) == 0 ) {
		sieve_address_error(ctx, "missing local part");
		return FALSE;
	}
	
	return TRUE;
}

static bool parse_domain(struct sieve_address_parser_context *ctx)
{
	int ret;

	str_truncate(ctx->str, 0);
	if ((ret = rfc822_parse_domain(&ctx->parser, ctx->str)) < 0 ||
		str_len(ctx->str) == 0 ) {
		sieve_address_error(ctx, "invalid or missing domain");
		
		return FALSE;
	}
	
	return TRUE;
}

static bool parse_addr_spec(struct sieve_address_parser_context *ctx)
{
	/* addr-spec       = local-part "@" domain */

	if ( !parse_local_part(ctx) )
		return FALSE;

	if ( *ctx->parser.data != '@') { 
		sieve_address_error(ctx, "expecting '@' after local part");
		return FALSE;
	}
		
	return parse_domain(ctx);
}

static int parse_name_addr(struct sieve_address_parser_context *ctx)
{
	/* phrase "<" addr-spec ">" ; name & addr-spec */
	   
	str_truncate(ctx->str, 0);
	if ( rfc822_parse_phrase(&ctx->parser, ctx->str) <= 0 ||
		*ctx->parser.data != '<' )
		return -1;
		
	ctx->parser.data++;

	/* "<" local-part "@" domain ">" */
	
	if ( !parse_addr_spec(ctx) )
		return FALSE;
			
	if (*ctx->parser.data != '>') {
		sieve_address_error(ctx, "missing '>'");
		return FALSE;
	}
	ctx->parser.data++;

	if (rfc822_skip_lwsp(&ctx->parser) <= 0)
		return FALSE;

	return TRUE;
}

static bool parse_sieve_address(struct sieve_address_parser_context *ctx)
{
	const unsigned char *start;
	int ret;

	if (ctx->parser.data == ctx->parser.end) {
		sieve_address_error(ctx, "empty address");
		return FALSE;
	}
	
	/* sieve-address   =       addr-spec                  ; simple address
   *                         / phrase "<" addr-spec ">" ; name & addr-spec
   */
 
	start = ctx->parser.data;
	ret = parse_name_addr(ctx);
	
	if ( ret < 0 ) {
		ctx->parser.data = start;
		return parse_addr_spec(ctx);
	}

	return ret > 0;
}

bool sieve_validate_address
(struct sieve_validator *valdtr, struct sieve_ast_node *node,
	string_t *address)
{
	struct sieve_address_parser_context ctx;

	memset(&ctx, 0, sizeof(ctx));
	rfc822_parser_init(&ctx.parser, str_data(address), str_len(address), 
		t_str_new(128));
	ctx.valdtr = valdtr;
	ctx.node = node;
	ctx.address = address;
	ctx.str = t_str_new(128);

	rfc822_skip_lwsp(&ctx.parser);
	
	return parse_sieve_address(&ctx);
}


