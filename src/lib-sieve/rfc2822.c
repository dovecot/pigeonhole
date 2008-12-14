/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file 
 */

#include "lib.h"
#include "str.h"

#include "rfc2822.h"

#include <stdio.h>
#include <ctype.h>

/* NOTE: much of the functionality implemented here should eventually appear
 * somewhere in Dovecot itself.
 */
 
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
	const char *sp = body, *bp = body, *wp;
	unsigned int len = strlen(name);
	
	/* Write header field name first */
	fwrite(name, len, 1, f);
	fwrite(": ", 2, 1, f);
		
	/* Add folded field body */
	len +=  2;
	while ( *bp != '\0' ) {
		while ( *bp != '\0' && (wp == NULL || len < 80) ) {
			if ( *bp == ' ' || *bp == '\t' ) 
			 	wp = bp;		

			bp++; len++;
		} 
		
		if ( *bp == '\0' ) break;
		
		fwrite(sp, wp-sp, 1, f);
		fwrite("\r\n", 2, 1, f);

		len = bp - wp;
		sp = wp;		
		wp = NULL;
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

