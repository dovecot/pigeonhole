/* Copyright (c) 2016 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "rfc822-parser.h"

#include "sieve-common.h"

#include "ext-vnd-report-common.h"

const char *
ext_vnd_report_parse_feedback_type(const char *feedback_type)
{
	struct rfc822_parser_context parser;
	string_t *token;

	/* Initialize parsing */
	rfc822_parser_init(&parser,
		(const unsigned char *)feedback_type, strlen(feedback_type), NULL);
	(void)rfc822_skip_lwsp(&parser);

	/* Parse MIME token */
	token = t_str_new(64);
	if (rfc822_parse_mime_token(&parser, token) < 0)
		return NULL;

	/* Content-type value must end here, otherwise it is invalid after all */
	(void)rfc822_skip_lwsp(&parser);
	if ( parser.data != parser.end )
		return NULL;

	/* Success */
	return str_c(token);
}
