/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

/* NOTE: much of the functionality implemented here should eventually appear
 * somewhere in Dovecot itself.
 */

#include "lib.h"
#include "str.h"
#include "unichar.h"

#include "rfc2822.h"

#include "message-header-encode.h"

#include <stdio.h>
#include <ctype.h>

bool rfc2822_header_field_name_verify
(const char *field_name, unsigned int len)
{
	const char *p = field_name;
	const char *pend = p + len;

	/* field-name   =   1*ftext
	 * ftext        =   %d33-57 /               ; Any character except
	 *                  %d59-126                ;  controls, SP, and
	 *                                          ;  ":".
	 */

	while ( p < pend ) {
		if ( *p < 33 || *p == ':' )
			return FALSE;

		p++;
	}

	return TRUE;
}

bool rfc2822_header_field_body_verify
(const char *field_body, unsigned int len, bool allow_crlf, bool allow_utf8)
{
	const unsigned char *p = (const unsigned char *)field_body;
	const unsigned char *pend = p + len;
	bool is8bit = FALSE;

	/* RFC5322:
	 *
	 * unstructured    =  (*([FWS] VCHAR) *WSP)
	 * VCHAR           =  %x21-7E
	 * FWS             =  ([*WSP CRLF] 1*WSP) /   ; Folding white space
	 * WSP             =  SP / HTAB               ; White space
	 */

	while ( p < pend ) {
		if ( *p < 0x20 ) {
			if ( (*p == '\r' || *p == '\n') ) {
				if ( !allow_crlf )
					return FALSE;
			} else if ( *p != '\t' ) {
				return FALSE;
			}
		}

		if ( !is8bit && *p > 127 ) {
			if ( !allow_utf8 )
				return FALSE;

			is8bit = TRUE;
		}

		p++;
	}

	if ( is8bit && !uni_utf8_str_is_valid(field_body) ) {
		return FALSE;
	}

	return TRUE;
}

/*
 *
 */

const char *rfc2822_header_field_name_sanitize(const char *name)
{
	char *result = t_strdup_noconst(name);
	char *p;

	/* Make the whole name lower case ... */
	result = str_lcase(result);

	/* ... except for the first letter and those that follow '-' */
	p = result;
	*p = i_toupper(*p);
	while ( *p != '\0' ) {
		if ( *p == '-' ) {
			p++;

			if ( *p != '\0' )
				*p = i_toupper(*p);

			continue;
		}

		p++;
	}

	return result;
}

/*
 * Message construction
 */

/* FIXME: This should be collected into a Dovecot API for composing internet
 * mail messages.
 */

unsigned int rfc2822_header_append
(string_t *header, const char *name, const char *body, bool crlf,
	uoff_t *body_offset_r)
{
	static const unsigned int max_line = 80;

	const char *bp = body;  /* Pointer */
	const char *sp = body;  /* Start pointer */
	const char *wp = NULL;  /* Whitespace pointer */
	const char *nlp = NULL; /* New-line pointer */
	unsigned int line_len = strlen(name);
	unsigned int lines = 0;

	/* Write header field name first */
	str_append_n(header, name, line_len);
	str_append_n(header, ": ", 2);

	if ( body_offset_r != NULL )
		*body_offset_r = str_len(header);

	line_len +=  2;

	/* Add field body; fold it if necessary and account for existing folding */
	while ( *bp != '\0' ) {
		while ( *bp != '\0' && nlp == NULL &&
			(wp == NULL || line_len < max_line) ) {
			if ( *bp == ' ' || *bp == '\t' ) {
			 	wp = bp;
			} else if ( *bp == '\r' || *bp == '\n' ) {
				nlp = bp;
				break;
			}

			bp++; line_len++;
		}

		if ( *bp == '\0' ) break;

		/* Existing newline ? */
		if ( nlp != NULL ) {
			/* Replace any sort of newline for consistency */
			while ( *bp == '\r' || *bp == '\n' )
				bp++;

			str_append_n(header, sp, nlp-sp);

			if ( *bp != '\0' && *bp != ' ' && *bp != '\t' ) {
				if ( crlf )
					str_append_n(header, "\r\n\t", 3);
				else
					str_append_n(header, "\n\t", 2);
			} else {
				if ( crlf )
					str_append_n(header, "\r\n", 2);
				else
					str_append_n(header, "\n", 1);
			}

			sp = bp;
		} else {
			/* Insert newline at last whitespace within the max_line limit */
			str_append_n(header, sp, wp-sp);
			if ( crlf )
				str_append_n(header, "\r\n", 2);
			else
				str_append_n(header, "\n", 1);

			sp = wp;
		}

		lines++;

		line_len = bp - sp;
		wp = NULL;
		nlp = NULL;
	}

	if ( bp != sp || lines == 0 ) {
		str_append_n(header, sp, bp-sp);
		if ( crlf )
			str_append_n(header, "\r\n", 2);
		else
			str_append_n(header, "\n", 1);
		lines++;
	}

	return lines;
}

void rfc2822_header_printf
(string_t *header, const char *name, const char *fmt, ...)
{
	const char *body;
	va_list args;

	va_start(args, fmt);
	body = t_strdup_vprintf(fmt, args);
	va_end(args);

	rfc2822_header_write(header, name, body);
}

void rfc2822_header_utf8_printf
(string_t *header, const char *name, const char *fmt, ...)
{
	string_t *body = t_str_new(256);
	va_list args;

	va_start(args, fmt);
	message_header_encode(t_strdup_vprintf(fmt, args), body);
	va_end(args);

	rfc2822_header_write(header, name, str_c(body));
}

