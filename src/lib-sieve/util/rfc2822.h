/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
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

unsigned int rfc2822_header_append
	(string_t *header, const char *name, const char *body, bool crlf,
		uoff_t *body_offset_r);

static inline void rfc2822_header_write
(string_t *header, const char *name, const char *body)
{
	(void)rfc2822_header_append(header, name, body, TRUE, NULL);
}

void rfc2822_header_printf
	(string_t *header, const char *name, const char *fmt, ...) ATTR_FORMAT(3, 4);
void rfc2822_header_utf8_printf
	(string_t *header, const char *name, const char *fmt, ...) ATTR_FORMAT(3, 4);

#endif /* __RFC2822_H */
