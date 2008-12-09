/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file 
 */

#ifndef __RFC2822_H
#define __RFC2822_H

#include "lib.h"

bool rfc2822_header_field_name_verify
	(const char *field_name, unsigned int len);

#endif /* __RFC2822_H */
