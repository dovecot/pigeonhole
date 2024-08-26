/* Copyright (c) 2016-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "settings.h"
#include "rfc822-parser.h"

#include "sieve-common.h"
#include "sieve-extensions.h"

#include "ext-vnd-report-common.h"

int ext_report_load(const struct sieve_extension *ext, void **context_r)
{
	struct sieve_instance *svinst = ext->svinst;
	struct ext_report_context *extctx;
	const struct ext_report_settings *set;
	const char *error;

	if (settings_get(svinst->event, &ext_report_setting_parser_info, 0,
			 &set, &error) < 0) {
		e_error(svinst->event, "%s", error);
		return -1;
	}

	extctx = i_new(struct ext_report_context, 1);
	extctx->set = set;

	*context_r = extctx;
	return 0;
}

void ext_report_unload(const struct sieve_extension *ext)
{
	struct ext_report_context *extctx = ext->context;

	if (extctx == NULL)
		return;
	settings_free(extctx->set);
	i_free(extctx);
}

const char *ext_vnd_report_parse_feedback_type(const char *feedback_type)
{
	struct rfc822_parser_context parser;
	string_t *token;

	/* Initialize parsing */
	rfc822_parser_init(&parser, (const unsigned char *)feedback_type,
			   strlen(feedback_type), NULL);
	(void)rfc822_skip_lwsp(&parser);

	/* Parse MIME token */
	token = t_str_new(64);
	if (rfc822_parse_mime_token(&parser, token) < 0)
		return NULL;

	/* Content-type value must end here, otherwise it is invalid after all.
	 */
	(void)rfc822_skip_lwsp(&parser);
	if (parser.data != parser.end)
		return NULL;

	/* Success */
	return str_c(token);
}
