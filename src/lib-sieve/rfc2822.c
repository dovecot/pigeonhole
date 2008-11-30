/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file 
 */

#include "lib.h"

#include "rfc2822.h"

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
	}	
	
	return TRUE;
}

