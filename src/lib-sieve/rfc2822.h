/* Copyright (c) 2002-2012 Pigeonhole authors, see the included COPYING file 
 */

#ifndef __RFC2822_H
#define __RFC2822_H

#include "lib.h"

#include <stdio.h>

/*
 * Verification
 */ 
 
bool rfc2822_header_field_name_verify
	(const char *field_name, unsigned int len);
bool rfc2822_header_field_body_verify
	(const char *field_body, unsigned int len, bool allow_crlf, bool allow_utf8);

/*
 *
 */

const char *rfc2822_header_field_name_sanitize(const char *name);

/*
 * Message composition
 */

unsigned int rfc2822_header_field_append
	(string_t *header, const char *name, const char *body, bool crlf,
		uoff_t *body_offset_r);

int rfc2822_header_field_write
	(FILE *f, const char *name, const char *body);
	
int rfc2822_header_field_printf
	(FILE *f, const char *name, const char *body_fmt, ...) ATTR_FORMAT(3, 4);

int rfc2822_header_field_utf8_printf
	(FILE *f, const char *name, const char *body_fmt, ...) ATTR_FORMAT(3, 4);

#endif /* __RFC2822_H */
