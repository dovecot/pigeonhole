/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file 
 */

/* NOTE: much of the functionality implemented here should eventually appear
 * somewhere in Dovecot itself.
 */

#include "lib.h"
#include "str.h"

#include "rfc2822.h"

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
(const char *field_body, unsigned int len) 
{
	const char *p = field_body;
	const char *pend = p + len;

	/* unstructured    =       *([FWS] utext) [FWS]
	 * FWS             =       ([*WSP CRLF] 1*WSP) /   ; Folding white space
	 *                         obs-FWS
	 * utext           =       NO-WS-CTL /     ; Non white space controls
	 *                         %d33-126 /      ; The rest of US-ASCII
	 *                         obs-utext
	 * NO-WS-CTL       =       %d1-8 /         ; US-ASCII control characters
	 *                         %d11 /          ;  that do not include the
	 *                         %d12 /          ;  carriage return, line feed,
	 *                         %d14-31 /       ;  and white space characters
	 *                         %d127
	 * WSP             =  SP / HTAB
	 */

	/* This verification does not allow content to be folded. This should done
	 * automatically upon message composition.
	 */

	while ( p < pend ) {
		if ( *p == '\0' || *p == '\r' || *p == '\n' || ((unsigned char)*p) > 127 )
			return FALSE;

		p++;
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
 * mail messages. These functions now use FILE * output streams, but this should
 * be changed to proper dovecot streams.
 */

void rfc2822_header_field_write
(FILE *f, const char *name, const char *body)
{
	static const unsigned int max_line = 80;
	
	const char *bp = body;  /* Pointer */ 
	const char *sp = body;  /* Start pointer */
	const char *wp = NULL;  /* Whitespace pointer */ 
	const char *nlp = NULL; /* New-line pointer */
	unsigned int len = strlen(name);
	
	/* Write header field name first */
	fwrite(name, len, 1, f);
	fwrite(": ", 2, 1, f);
		
	/* Add field body; fold it if necessary and account for existing folding */
	len +=  2;
	while ( *bp != '\0' ) {
		while ( *bp != '\0' && nlp == NULL && (wp == NULL || len < max_line) ) {
			if ( *bp == ' ' || *bp == '\t' ) {
			 	wp = bp;
			} else if ( *bp == '\r' || *bp == '\n' ) {
				nlp = bp;			
				break;
			}

			bp++; len++;
		}
		
		if ( *bp == '\0' ) break;
		
		/* Existing newline ? */
		if ( nlp != NULL ) {
			/* Replace any sort of newline with proper CRLF */
			while ( *bp == '\r' || *bp == '\n' )
				bp++;
			
			fwrite(sp, nlp-sp, 1, f);
			
			if ( *bp != '\0' && *bp != ' ' && *bp != '\t' )
				fwrite("\r\n\t", 3, 1, f);
			else
				fwrite("\r\n", 2, 1, f);
				
			sp = bp;
		} else {
			/* Insert newline at last whitespace within the max_line limit */
			fwrite(sp, wp-sp, 1, f);
			fwrite("\r\n", 2, 1, f);
			sp = wp;
		}
		
		len = bp - sp;		
		wp = NULL;
		nlp = NULL;
	}
	
	fwrite(sp, bp-sp, 1, f);
	fwrite("\r\n", 2, 1, f);
}

void rfc2822_header_field_printf
(FILE *f, const char *name, const char *body_fmt, ...)
{
	string_t *body = t_str_new(256);
	va_list args;

	va_start(args, body_fmt);
	str_vprintfa(body, body_fmt, args);
	va_end(args);
	
	rfc2822_header_field_write(f, name, str_c(body));
}

