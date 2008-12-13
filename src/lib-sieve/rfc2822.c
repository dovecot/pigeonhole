/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file 
 */

#include "lib.h"
#include "str.h"

#include "rfc2822.h"

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

void rfc2822_header_add_folded
(string_t *out, const char *name, const char *body)
{
	const char *sp = body, *bp = body, *wp;
	unsigned int len = str_len(out); 
	
	/* Add properly formatted header field name first */
	str_append_c(out, i_toupper(name[0]));
	str_append(out, t_str_lcase(name+1));
	
	/* Add separating colon */
	str_append(out, ": ");
	
	/* Add folded field body */
	len = str_len(out) - len;
	while ( *bp != '\0' ) {
		while ( *bp != '\0' && (wp == NULL || len < 80) ) {
			if ( *bp == ' ' || *bp == '\t' ) 
			 	wp = bp;		

			bp++; len++;
		} 
		
		if ( *bp == '\0' ) break;
		
		str_append_n(out, sp, wp-sp);
		str_append(out, "\r\n"); 

		len = bp - wp;
		sp = wp;		
		wp = NULL;
	}
	
	str_append_n(out, sp, bp-sp);
	str_append(out, "\r\n"); 
}

