#include "lib.h"
#include "str.h"
#include "rfc822-parser.h"

#include "sieve-common.h"

#include "sieve-address.h"

#include <ctype.h>

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
 
struct sieve_message_address_parser {
	struct rfc822_parser_context parser;

	string_t *address;

	string_t *str;
	string_t *local_part;
	string_t *domain;
	
	string_t *error;
};

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
		str_c(ctx->local_part));
	return -1;
}

static int parse_name_addr(struct sieve_message_address_parser *ctx)
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

static bool parse_sieve_address(struct sieve_message_address_parser *ctx)
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
	struct sieve_message_address_parser ctx;

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
	struct sieve_message_address_parser ctx;

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

/*
 * Envelope address parsing
 *   RFC 2821
 */

#define AB (1<<0)
#define DB (1<<1)
#define QB (1<<2)

/* atext = ALPHA / DIGIT / "!" / "#" / "$" / "%"
 *         / "&" / "'" / "*" / "+" / "-" / "/" / "="
 *         / "?" / "^" / "_" / "`" / "{" / "|" / "}" / "~" 
 */
//#define IS_ATEXT(C) ((rfc2821_chars[C] & AB) != 0) 

/* dtext = NO-WS-CTL / %d33-90 / %d94-126 
 * NO-WS-CTL = %d1-8 / %d11 / %d12 / %d14-31 / %d127 
 */
#define IS_DTEXT(C) ((rfc2821_chars[C] & DB) != 0) 

/* qtext= NO-WS-CTL  / %d33 / %d35-91 / %d93-126 */
#define IS_QTEXT(C) ((rfc2821_chars[C] & QB) == 0) 

/* text	= %d1-9 / %d11 / %d12 / %d14-127 / obs-text*/
#define IS_TEXT(C) ((C) != '\r' && (C) != '\n' && (C) < 128)

static unsigned char rfc2821_chars[256] = {
	   DB,    DB,    DB,    DB,    DB,    DB,    DB,    DB, // 0
	   DB,    QB,    QB,    DB,    DB,    QB,    DB,    DB, // 8
       DB,    DB,    DB,    DB,    DB,    DB,    DB,    DB, // 16
	   DB,    DB,    DB,    DB,    DB,    DB,    DB,    DB, // 24
	   QB, DB|AB, QB|DB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, // 32
	   DB,    DB, DB|AB, DB|AB,    DB, DB|AB,    DB, DB|AB, // 40
	   DB,    DB,    DB,    DB,    DB,    DB,    DB,    DB, // 48
	   DB,    DB,    DB,    DB,    DB, DB|AB,    DB, DB|AB, // 56
	   DB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, // 64
	DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, // 72
	DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, // 80
	DB|AB, DB|AB, DB|AB,     0,    QB,     0, DB|AB, DB|AB, // 88
	DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, // 96
	DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, // 104
	DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, // 112
	DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|AB, DB|QB, // 120

	0, 0, 0, 0, 0, 0, 0, 0, // 128
	0, 0, 0, 0, 0, 0, 0, 0, // 136
	0, 0, 0, 0, 0, 0, 0, 0, // 144
	0, 0, 0, 0, 0, 0, 0, 0, // 152
	0, 0, 0, 0, 0, 0, 0, 0, // 160
	0, 0, 0, 0, 0, 0, 0, 0, // 168
	0, 0, 0, 0, 0, 0, 0, 0, // 176
	0, 0, 0, 0, 0, 0, 0, 0, // 184
	0, 0, 0, 0, 0, 0, 0, 0, // 192
	0, 0, 0, 0, 0, 0, 0, 0, // 200
	0, 0, 0, 0, 0, 0, 0, 0, // 208
	0, 0, 0, 0, 0, 0, 0, 0, // 216
	0, 0, 0, 0, 0, 0, 0, 0, // 224
	0, 0, 0, 0, 0, 0, 0, 0, // 232
	0, 0, 0, 0, 0, 0, 0, 0, // 240
	0, 0, 0, 0, 0, 0, 0, 0, // 248

};

struct sieve_envelope_address_parser {
	const unsigned char *data;
	const unsigned char *end;

	string_t *str;

	struct sieve_address *address;
};

static int path_skip_white_space(struct sieve_envelope_address_parser *parser)
{
	/* Not mentioned anywhere in the specification, but we do it any way
	 * (e.g. Exim does so too)
	 */
	while ( parser->data < parser->end && 
		(*parser->data == ' ' || *parser->data == '\t') )
		parser->data++;

	return parser->data < parser->end;
}

static int path_skip_address_literal
(struct sieve_envelope_address_parser *parser)
{
	int count;

	/* Currently we are oblivious to address syntax:
	 * address-literal = "[" 1*dcontent "]"
	 * dcontent	= dtext / quoted-pair
	 */

	i_assert ( *parser->data == '[' );

	str_append_c(parser->str, *parser->data);
	parser->data++;

	while ( parser->data < parser->end ) {
		if ( *parser->data == '\\' ) {
			str_append_c(parser->str, *parser->data);
			parser->data++;
				
			if ( parser->data < parser->end ) {
				if ( !IS_TEXT(*parser->data) ) 
					return -1;

				str_append_c(parser->str, *parser->data);
				parser->data++;
			} else return -1;
		} else {
			if ( !IS_DTEXT(*parser->data) )
				break;

			str_append_c(parser->str, *parser->data);
			parser->data++;
		}

		count++;
	}

		
	if ( count == 0 || parser->data >= parser->end || *parser->data != ']' )
		return -1;

	str_append_c(parser->str, *parser->data);
	parser->data++;

	return parser->data < parser->end;
}

static int path_parse_domain
(struct sieve_envelope_address_parser *parser, bool skip)
{
	int ret;

	/* Domain = (sub-domain 1*("." sub-domain)) / address-literal
     * sub-domain = Let-dig [Ldh-str]
	 * Let-dig = ALPHA / DIGIT
	 * Ldh-str = *( ALPHA / DIGIT / "-" ) Let-dig
	 */
	
	str_truncate(parser->str, 0);
	if ( *parser->data == '[' ) {
		ret = path_skip_address_literal(parser);

		if ( ret < 0 ) return ret;
	} else {
		for (;;) {
			if ( !i_isalnum(*parser->data) )
				return -1;

			str_append_c(parser->str, *parser->data);
			parser->data++;

			while ( parser->data < parser->end ) {
				if ( !i_isalnum(*parser->data) && *parser->data != '-' )
					break;

				str_append_c(parser->str, *parser->data);
				parser->data++;
			}

			if ( !i_isalnum(*(parser->data-1)) )
				return -1;
			
			if ( (ret=path_skip_white_space(parser)) < 0 )
	            return ret;

			if ( *parser->data != '.' )
                break;

			str_append_c(parser->str, *parser->data);
            parser->data++;

			if ( (ret=path_skip_white_space(parser)) <= 0 )
	            return -1;
		}
	}

	if ( !skip )
		parser->address->domain = t_strdup(str_c(parser->str));

	return path_skip_white_space(parser);
}

static int path_skip_source_route(struct sieve_envelope_address_parser *parser)
{
	int ret;

	/* A-d-l = At-domain *( "," A-d-l )
	 * At-domain = "@" domain
	 */

	if ( *parser->data == '@' ) {
		parser->data++;
	
		for (;;) {
			if ( (ret=path_skip_white_space(parser)) <= 0 )
		        return -1;	

			if ( (ret=path_parse_domain(parser, TRUE)) <= 0 )
		        return -1;	

			if ( (ret=path_skip_white_space(parser)) <= 0 )
            	return ret;

			/* Next? */
			if ( *parser->data != ',' )
				break;
			parser->data++;

			if ( (ret=path_skip_white_space(parser)) <= 0 )
        	    return -1;

			if ( *parser->data != '@' )
				return -1;
			parser->data++;
		}
	}

	return parser->data < parser->end;
}

static int path_parse_local_part(struct sieve_envelope_address_parser *parser)
{
	int ret;
	/* Local-part = Dot-string / Quoted-string
     * Dot-string = Atom *("." Atom)
     * Atom = 1*atext
     * Quoted-string = DQUOTE *qcontent DQUOTE
     * qcontent = qtext / quoted-pair
     * quoted-pair  =   ("\" text)
     */

	str_truncate(parser->str, 0);
    if ( *parser->data == '"' ) {
		str_append_c(parser->str, *parser->data);
        parser->data++;

		while ( parser->data < parser->end ) {
			if ( *parser->data == '\\' ) {
                str_append_c(parser->str, *parser->data);
                parser->data++;

                if ( parser->data < parser->end ) {
                    if ( !IS_TEXT(*parser->data) )
                        return -1;

                    str_append_c(parser->str, *parser->data);
                    parser->data++;
                } else return -1;
            } else {
                if ( !IS_QTEXT(*parser->data) )
                	break;

                str_append_c(parser->str, *parser->data);
                parser->data++;
            }
		}
		
		if ( *parser->data != '"' )
			return -1;

		str_append_c(parser->str, *parser->data);
        parser->data++;
		
		if ( (ret=path_skip_white_space(parser)) < 0 )
			return ret;
    } else {
       for (;;) {
			if ( !IS_ATEXT(*parser->data) ) 
				return -1;
			str_append_c(parser->str, *parser->data);
			parser->data++;

			while ( parser->data < parser->end && IS_ATEXT(*parser->data)) {
				str_append_c(parser->str, *parser->data);
				parser->data++;
			}
			
			if ( (ret=path_skip_white_space(parser)) < 0 )
		        return ret;

			if ( *parser->data != '.' )
				break;

			str_append_c(parser->str, *parser->data);
   	        parser->data++;
	
			if ( (ret=path_skip_white_space(parser)) <= 0 )
		        return -1;
		}
    }

	parser->address->local_part = t_strdup(str_c(parser->str));
	return parser->data < parser->end;
}

static int path_parse_mailbox(struct sieve_envelope_address_parser *parser)
{
	int ret;

	/* Mailbox = Local-part "@" Domain */
	
	if ( (ret=path_parse_local_part(parser)) <= 0 )
        return ret;

	if ( (ret=path_skip_white_space(parser)) <= 0 )
        return -1;

	if ( *parser->data != '@' )
		return -1;
	parser->data++;

	 if ( (ret=path_skip_white_space(parser)) <= 0 )
        return -1;

	return path_parse_domain(parser, FALSE);
}

static int path_parse(struct sieve_envelope_address_parser *parser)
{
	int ret;
	bool brackets = FALSE;

	if ( (ret=path_skip_white_space(parser)) <= 0 ) 
		return ret;
	
    /* We allow angle brackets to be missing */
    if ( *parser->data == '<' ) {
        parser->data++;
		brackets = TRUE;

		if ( (ret=path_skip_white_space(parser)) <= 0 ) 
			return -1;

		/* Null path? */
		if ( *parser->data == '>' ) {
			parser->data++;
			return path_skip_white_space(parser);
		}
	}

	/*  [ A-d-l ":" ] Mailbox */

	if ( (ret=path_skip_source_route(parser)) <= 0 )
		return -1;

	if ( (ret=path_parse_mailbox(parser)) < 0 )
		return -1;

	if ( ret > 0 && (ret=path_skip_white_space(parser)) < 0 ) 
		return -1;

	if ( brackets ) {
		if ( ret <= 0 ) return -1;

		if ( *parser->data != '>' )
			return -1;
		parser->data++;
	}

	return parser->data < parser->end;
}

const struct sieve_address *sieve_address_parse_envelope_path
(const char *field_value)
{
	struct sieve_envelope_address_parser parser;
	int ret;

	parser.data = (const unsigned char *) field_value;
    parser.end = (const unsigned char *) field_value + strlen(field_value);
	parser.address = t_new(struct sieve_address, 1);
	parser.str = t_str_new(256);

	if ( (ret=path_parse(&parser)) < 0 )
		return NULL;
	
	if ( ret > 0 && path_skip_white_space(&parser) < 0 )
		return NULL;

	if ( parser.data != parser.end )
		return NULL;

	return parser.address;
}


