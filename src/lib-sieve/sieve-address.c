#include "lib.h"
#include "str.h"
#include "message-parser.h"
#include "message-address.h"
#include "rfc822-parser.h"

/* WARNING: This file contains code duplicated from dovecot/src/lib-mail/message-address.c */

/* FIXME: Currently accepts only c-strings and no \0 characters (not according to spec) 
 */

#ifdef TEST
#include <stdio.h>
#endif

/* Sieve address as defined in RFC3028 [SIEVE] and RFC822 [IMAIL]:
 *
 * sieve-address      = addr-spec                                     ; simple address
 *                    / phrase "<" addr-spec ">"                      ; name & addr-spec
 * addr-spec          =  local-part "@" domain                        ; global address
 * local-part         =  word *("." word)                             ; uninterpreted
 *                                                                    ; case-preserved
 * domain             =  sub-domain *("." sub-domain)
 * sub-domain         =  domain-ref / domain-literal
 * domain-literal     =  "[" *(dtext / quoted-pair) "]"
 * domain-ref         =  atom 
 * dtext              =  <any CHAR excluding "[", "]", "\" & CR, &    ; => may be folded
 *                       including linear-white-space>
 * phrase             =  1*word                                       ; Sequence of words
 * word               =  atom / quoted-string
 * quoted-string      = <"> *(qtext/quoted-pair) <">                  ; Regular qtext or
 *                                                                    ;   quoted chars.
 * quoted-pair        =  "\" CHAR                                     ; may quote any char
 * qtext              =  <any CHAR excepting <">,                     ; => may be folded
 *                       "\" & CR, and including
 *                       linear-white-space>
 * atom               =  1*<any CHAR except specials, SPACE and CTLs>
 * specials           =  "(" / ")" / "<" / ">" / "@"                  ; Must be in quoted-
 *                    /  "," / ";" / ":" / "\" / <">                  ;  string, to use
 *                    /  "." / "[" / "]"                              ;  within a word.
 * linear-white-space =  1*([CRLF] LWSP-char)                         ; semantics = SPACE
 *                                                                    ; CRLF => folding
 * LWSP-char          =  SPACE / HTAB                                 ; semantics = SPACE
 * CTL                =  <any ASCII control                           ; (  0- 37,  0.- 31.)
 *                       character and DEL>                           ; (    177,     127.)
 * CR                 =  <ASCII CR, carriage return>                  ; (     15,      13.)
 * SPACE              =  <ASCII SP, space>                            ; (     40,      32.)
 * HTAB               =  <ASCII HT, horizontal-tab>                   ; (     11,       9.)
 * CHAR               =  <any ASCII character>                        ; (  0-177,  0.-127.)
 * <">                =  <ASCII quote mark>                           ; (     42,      34.)
 *  
 * Note:  For purposes of display, and when passing  such  structured information to 
 *        other systems, such as mail protocol  services,  there  must  be  NO  
 *        linear-white-space between  <word>s  that are separated by period (".") or
 *        at-sign ("@") and exactly one SPACE between  all  other <word>s. 
 */

struct sieve_address_parser_context {
	pool_t pool;
	struct rfc822_parser_context parser;
	
	const char *name, *mailbox, *domain;
	
	string_t *str;
};

static int parse_local_part(struct sieve_address_parser_context *ctx)
{
	int ret;

	/*
	   local-part      = dot-atom / quoted-string / obs-local-part
	   obs-local-part  = word *("." word)
	*/
	if (ctx->parser.data == ctx->parser.end)
		return 0;

	str_truncate(ctx->str, 0);
	if (*ctx->parser.data == '"')
		ret = rfc822_parse_quoted_string(&ctx->parser, ctx->str);
	else
		ret = rfc822_parse_dot_atom(&ctx->parser, ctx->str);
	if (ret < 0)
		return -1;

	ctx->mailbox = p_strdup(ctx->pool, str_c(ctx->str));
	return ret;
}

static int parse_domain(struct sieve_address_parser_context *ctx)
{
	int ret;

	str_truncate(ctx->str, 0);
	if ((ret = rfc822_parse_domain(&ctx->parser, ctx->str)) < 0)
		return -1;

	ctx->domain = p_strdup(ctx->pool, str_c(ctx->str));
	return ret;
}

static int parse_angle_addr(struct sieve_address_parser_context *ctx)
{
	int ret;

	/* "<" local-part "@" domain ">" */
	i_assert(*ctx->parser.data == '<');
	ctx->parser.data++;

	if ((ret = rfc822_skip_lwsp(&ctx->parser)) <= 0)
		return ret;

	if ((ret = parse_local_part(ctx)) <= 0)
		return ret;
		
	if (*ctx->parser.data == '@') {
		if ((ret = parse_domain(ctx)) <= 0)
			return ret;
	}

	if (*ctx->parser.data != '>')
		return -1;
	ctx->parser.data++;

	return rfc822_skip_lwsp(&ctx->parser);
}

static int parse_name_addr(struct sieve_address_parser_context *ctx)
{
	/* phrase "<" addr-spec ">"     ; name & addr-spec	*/
	str_truncate(ctx->str, 0);
	if (rfc822_parse_phrase(&ctx->parser, ctx->str) <= 0 ||
	    *ctx->parser.data != '<')
		return -1;

	ctx->addr.name = p_strdup(ctx->pool, str_c(ctx->str));
	if (*ctx->addr.name == '\0') {
		/* Cope with "<address>" without display name */
		ctx->addr.name = NULL;
	}
	if (parse_angle_addr(ctx) < 0) {
		/* broken */
		ctx->addr.domain = p_strdup(ctx->pool, "SYNTAX_ERROR");
	}
	return ctx->parser.data != ctx->parser.end;
}

static int parse_addr_spec(struct message_address_parser_context *ctx)
{
	/* addr-spec       = local-part "@" domain */
	int ret;

	str_truncate(ctx->parser.last_comment, 0);

	if ((ret = parse_local_part(ctx)) < 0)
		return ret;
	if (ret > 0 && *ctx->parser.data == '@') {
		if ((ret = parse_domain(ctx)) < 0)
			return ret;
	}

	if (str_len(ctx->parser.last_comment) > 0) {
		ctx->addr.name =
			p_strdup(ctx->pool, str_c(ctx->parser.last_comment));
	}
	return ret;
}

static int sieve_address_parse(pool_t pool, const unsigned char *data, size_t size)
{
	struct sieve_address_parser_context ctx;
	const unsigned char *start;
	int ret;

	if (!pool->datastack_pool)
		t_push();
		
	memset(&ctx, 0, sizeof(ctx));

	rfc822_parser_init(&ctx.parser, data, size, t_str_new(128));
	ctx.pool = pool;
	ctx.str = t_str_new(128);

	rfc822_skip_lwsp(&ctx.parser);
	if (ctx->parser.data == ctx->parser.end)
		return 0;

 	/* sieve-address      = addr-spec                    ; simple address
 	 *                    / phrase "<" addr-spec ">"     ; name & addr-spec
 	 */
	start = ctx->parser.data; 
	if ((ret = parse_name_addr(ctx)) < 0) {
		/* nope, should be addr-spec */
		ctx->parser.data = start;
		if ((ret = parse_addr_spec(ctx)) < 0)
			return -1;
	}

	if (!pool->datastack_pool
		t_pop();
	return ret;
}


#endif 

