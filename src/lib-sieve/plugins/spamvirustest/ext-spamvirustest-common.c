/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "strfuncs.h"
#include "mail-storage.h"
#include "settings.h"

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-extensions.h"
#include "sieve-message.h"
#include "sieve-interpreter.h"
#include "sieve-runtime-trace.h"

#include "ext-spamvirustest-settings.h"
#include "ext-spamvirustest-common.h"

#include <sys/types.h>
#include <ctype.h>
#include <regex.h>

/*
 * Extension data
 */

struct ext_spamvirustest_header_spec {
	const char *header_name;
	regex_t regexp;
	bool regexp_match;
};

struct ext_spamvirustest_context {
	pool_t pool;
	unsigned int reload_id;
	const struct ext_spamvirustest_settings *set;

	struct ext_spamvirustest_header_spec status_header;
	struct ext_spamvirustest_header_spec score_max_header;
};

/*
 * Regexp utility
 */

static bool
_regexp_compile(regex_t *regexp, const char *data, const char **error_r)
{
	size_t errsize;
	int ret;

	*error_r = "";

	ret = regcomp(regexp, data, REG_EXTENDED);
	if (ret == 0)
		return TRUE;

	errsize = regerror(ret, regexp, NULL, 0);

	if (errsize > 0) {
		char *errbuf = t_malloc0(errsize);

		(void)regerror(ret, regexp, errbuf, errsize);

		/* We don't want the error to start with a capital letter */
		errbuf[0] = i_tolower(errbuf[0]);

		*error_r = errbuf;
	}
	return FALSE;
}

static const char *
_regexp_match_get_value(const char *string, int index, regmatch_t pmatch[],
			int nmatch)
{
	if (index > -1 && index < nmatch && pmatch[index].rm_so != -1) {
		return t_strndup(string + pmatch[index].rm_so,
				 pmatch[index].rm_eo - pmatch[index].rm_so);
	}
	return NULL;
}

/*
 * Configuration parser
 */

static bool
ext_spamvirustest_header_spec_parse(
	pool_t pool, const char *data,
	struct ext_spamvirustest_header_spec *spec_r, const char **error_r)
{
	const char *p;
	const char *regexp_error;

	if (*data == '\0') {
		*error_r = "empty header specification";
		return FALSE;
	}

	/* Parse header name */

	p = data;

	while (*p == ' ' || *p == '\t')
		p++;
	while (*p != ':' && *p != '\0' && *p != ' ' && *p != '\t')
		p++;

	if (*p == '\0') {
		spec_r->header_name = p_strdup(pool, data);
		return TRUE;
	}

	spec_r->header_name = p_strdup_until(pool, data, p);
	while (*p == ' ' || *p == '\t')
		p++;

	if (*p == '\0') {
		spec_r->regexp_match = FALSE;
		return TRUE;
	}

	/* Parse and compile regular expression */

	if (*p != ':') {
		*error_r = t_strdup_printf("expecting ':', but found '%c'", *p);
		return FALSE;
	}
	p++;
	while (*p == ' ' || *p == '\t') p++;

	spec_r->regexp_match = TRUE;
	if (!_regexp_compile(&spec_r->regexp, p, &regexp_error)) {
		*error_r = t_strdup_printf(
			"failed to compile regular expression '%s': %s",
			p, regexp_error);
		return FALSE;
	}
	return TRUE;
}

static void
ext_spamvirustest_header_spec_free(struct ext_spamvirustest_header_spec *spec)
{
	regfree(&spec->regexp);
}

static bool
ext_spamvirustest_parse_strlen_value(const char *str_value,
				     float *value_r, const char **error_r)
{
	const char *p = str_value;
	char ch = *p;

	if (*str_value == '\0') {
		*value_r = 0;
		return TRUE;
	}

	while (*p == ch)
		p++;

	if (*p != '\0') {
		*error_r = t_strdup_printf(
			"different character '%c' encountered in strlen value",
			*p);
		return FALSE;
	}

	*value_r = (p - str_value);
	return TRUE;
}

/*
 * Extension initialization
 */

int ext_spamvirustest_load(const struct sieve_extension *ext, void **context_r)
{
	static unsigned int reload_id = 0;
	struct sieve_instance *svinst = ext->svinst;
	struct ext_spamvirustest_context *extctx;
	const struct setting_parser_info *set_info;
	const struct ext_spamvirustest_settings *set;
	const char *error;
	pool_t pool;
	int ret = 0;

	/* Get settings */

	if (sieve_extension_is(ext, spamtest_extension) ||
	    sieve_extension_is(ext, spamtestplus_extension))
		set_info = &ext_spamtest_setting_parser_info;
	else if (sieve_extension_is(ext, virustest_extension))
		set_info = &ext_virustest_setting_parser_info;
	else
		i_unreached();

	if (settings_get(svinst->event, set_info, 0, &set, &error) < 0) {
		e_error(svinst->event, "%s", error);
		return -1;
	}

	/* Base configuration */

	if (*set->status_header == '\0') {
		settings_free(set);
		return 0;
	}

	pool = pool_alloconly_create("spamvirustest_data", 512);
	extctx = p_new(pool, struct ext_spamvirustest_context, 1);
	extctx->pool = pool;
	extctx->reload_id = ++reload_id;
	extctx->set = set;

	if (!ext_spamvirustest_header_spec_parse(extctx->pool,
						 set->status_header,
						 &extctx->status_header,
						 &error)) {
		e_error(svinst->event, "%s: "
			"Invalid status header specification '%s': %s",
			sieve_extension_name(ext), set->status_header, error);
		ret = -1;
	}

	if (ret == 0 &&
	    set->parsed.status_type != EXT_SPAMVIRUSTEST_STATUS_TYPE_TEXT &&
	    *set->score_max_header != '\0' &&
	    !ext_spamvirustest_header_spec_parse(extctx->pool,
						 set->score_max_header,
						 &extctx->score_max_header,
						 &error)) {
		e_error(svinst->event, "%s: "
			"Invalid max score header specification '%s': %s",
			sieve_extension_name(ext), set->score_max_header, error);
		ret = -1;
	}

	*context_r = extctx;
	if (ret < 0) {
		e_warning(svinst->event, "%s: "
			  "Extension not configured, "
			  "tests will always match against \"0\"",
			  sieve_extension_name(ext));
		ext_spamvirustest_unload(ext);
		*context_r = NULL;
	}

	return ret;
}

void ext_spamvirustest_unload(const struct sieve_extension *ext)
{
	struct ext_spamvirustest_context *extctx = ext->context;

	if (extctx == NULL)
		return;

	ext_spamvirustest_header_spec_free(&extctx->status_header);
	ext_spamvirustest_header_spec_free(&extctx->score_max_header);
	settings_free(extctx->set);
	pool_unref(&extctx->pool);
}

/*
 * Runtime
 */

struct ext_spamvirustest_message_context {
	unsigned int reload_id;
	float score_ratio;
};

static const char *
ext_spamvirustest_get_score(const struct sieve_extension *ext,
			    float score_ratio, bool percent)
{
	int score;

	if (score_ratio < 0)
		return "0";

	if (score_ratio > 1)
		score_ratio = 1;

	if (percent)
		score = score_ratio * 100 + 0.001;
	else if (sieve_extension_is(ext, virustest_extension))
		score = score_ratio * 4 + 1.001;
	else
		score = score_ratio * 9 + 1.001;

	return t_strdup_printf("%d", score);
}

int ext_spamvirustest_get_value(const struct sieve_runtime_env *renv,
				const struct sieve_extension *ext, bool percent,
				const char **value_r)
{
	struct ext_spamvirustest_context *extctx = ext->context;
	struct ext_spamvirustest_header_spec *status_header, *max_header;
	struct sieve_message_context *msgctx = renv->msgctx;
	struct ext_spamvirustest_message_context *mctx;
	struct mail *mail;
	regmatch_t match_values[2];
	const char *header_value, *error;
	const char *status = NULL, *max = NULL;
	float status_value, max_value;
	unsigned int i, max_text;
	pool_t pool = sieve_interpreter_pool(renv->interp);

	*value_r = "0";

	/*
	 * Check whether extension is properly configured
	 */

	if (extctx == NULL) {
		sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS,
				    "error: extension not configured");
		return SIEVE_EXEC_OK;
	}

	/*
	 * Check wether a cached result is available
	 */

	mctx = (struct ext_spamvirustest_message_context *)
		sieve_message_context_extension_get(msgctx, ext);

	if (mctx == NULL) {
		/* Create new context */
		mctx = p_new(pool, struct ext_spamvirustest_message_context, 1);
		sieve_message_context_extension_set(msgctx, ext, mctx);
	} else if (mctx->reload_id == extctx->reload_id) {
		/* Use cached result */
		*value_r = ext_spamvirustest_get_score(ext, mctx->score_ratio,
						       percent);
		return SIEVE_EXEC_OK;
	} else {
		/* Extension was reloaded (probably in testsuite) */
	}

	mctx->reload_id = extctx->reload_id;

	/*
	 * Get max status value
	 */

	mail = sieve_message_get_mail(renv->msgctx);
	status_header = &extctx->status_header;
	max_header = &extctx->score_max_header;

	if (extctx->set->parsed.status_type != EXT_SPAMVIRUSTEST_STATUS_TYPE_TEXT) {
		if (max_header->header_name != NULL) {
			/* Get header from message */
			if (mail_get_first_header_utf8(
				mail, max_header->header_name,
				&header_value) < 0) {
				return sieve_runtime_mail_error	(
					renv, mail, "%s test: "
					"failed to read header field '%s'",
					sieve_extension_name(ext),
					max_header->header_name);
			}
			if (header_value == NULL) {
				sieve_runtime_trace(
					renv, SIEVE_TRLVL_TESTS,
					"header '%s' not found in message",
					max_header->header_name);
				goto failed;
			}

			if (max_header->regexp_match) {
				/* Execute regex */
				if (regexec(&max_header->regexp, header_value,
					    2, match_values, 0) != 0 ) {
					sieve_runtime_trace(
						renv, SIEVE_TRLVL_TESTS,
						"score_max_header regexp for header '%s' did not match "
						"on value '%s'",
						max_header->header_name,
						header_value);
					goto failed;
				}

				max = _regexp_match_get_value(header_value, 1,
							      match_values, 2);
				if (max == NULL) {
					sieve_runtime_trace(
						renv, SIEVE_TRLVL_TESTS,
						"regexp did not return match value "
						"for string '%s'",
						header_value);
					goto failed;
				}
			} else {
				max = header_value;
			}

			if (!ext_spamvirustest_parse_decimal_value(
				max, &max_value, &error)) {
				sieve_runtime_trace(
					renv, SIEVE_TRLVL_TESTS,
					"failed to parse maximum value: %s",
					error);
				goto failed;
			}
		} else {
			max_value = extctx->set->parsed.score_max_value;
		}

		if (max_value == 0) {
			sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS,
					    "error: max value is 0");
			goto failed;
		}
	} else {
		max_value = (sieve_extension_is(ext, virustest_extension) ?
			     5 : 10);
	}

	/*
	 * Get status value
	 */

	/* Get header from message */
	if (mail_get_first_header_utf8(mail, status_header->header_name,
				       &header_value) < 0) {
		return sieve_runtime_mail_error(
			renv, mail, "%s test: failed to read header field '%s'",
			sieve_extension_name(ext), status_header->header_name);
	}
	if (header_value == NULL) {
		sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS,
				    "header '%s' not found in message",
				    status_header->header_name);
		goto failed;
	}

	/* Execute regex */
	if (status_header->regexp_match) {
		if (regexec(&status_header->regexp, header_value, 2,
			    match_values, 0) != 0 ) {
			sieve_runtime_trace(
				renv, SIEVE_TRLVL_TESTS,
				"status_header regexp for header '%s' did not match on value '%s'",
				status_header->header_name, header_value);
			goto failed;
		}

		status = _regexp_match_get_value(header_value, 1,
						 match_values, 2);
		if (status == NULL) {
			sieve_runtime_trace(
				renv, SIEVE_TRLVL_TESTS,
				"regexp did not return match value for string '%s'",
				header_value);
			goto failed;
		}
	} else {
		status = header_value;
	}

	switch (extctx->set->parsed.status_type) {
	case EXT_SPAMVIRUSTEST_STATUS_TYPE_SCORE:
		if (!ext_spamvirustest_parse_decimal_value(
			status, &status_value, &error)) {
			sieve_runtime_trace(
				renv, SIEVE_TRLVL_TESTS,
				"failed to parse status value '%s': %s",
				status, error);
			goto failed;
		}
		break;
	case EXT_SPAMVIRUSTEST_STATUS_TYPE_STRLEN:
		if (!ext_spamvirustest_parse_strlen_value(
			status, &status_value, &error)) {
			sieve_runtime_trace(
				renv, SIEVE_TRLVL_TESTS,
				"failed to parse status value '%s': %s",
				status, error);
			goto failed;
		}
		break;
	case EXT_SPAMVIRUSTEST_STATUS_TYPE_TEXT:
		max_text = (sieve_extension_is(ext, virustest_extension) ?
			    5 : 10);
		status_value = 0;

		i = 0;
		while (i <= max_text) {
			if (extctx->set->parsed.text_values[i] != NULL &&
			    strcmp(status,
				   extctx->set->parsed.text_values[i]) == 0) {
				status_value = (float)i;
				break;
			}
			i++;
		}

		if (i > max_text) {
			sieve_runtime_trace(
				renv, SIEVE_TRLVL_TESTS,
				"failed to match textstatus value '%s'",
				status);
			goto failed;
		}
		break;
	default:
		i_unreached();
	}

	/* Calculate value */
	if (status_value < 0)
		mctx->score_ratio = 0;
	else if (status_value > max_value)
		mctx->score_ratio = 1;
	else
		mctx->score_ratio = (status_value / max_value);

	sieve_runtime_trace(
		renv, SIEVE_TRLVL_TESTS,
		"extracted score=%.3f, max=%.3f, ratio=%.0f %%",
		status_value, max_value, mctx->score_ratio * 100);

	*value_r = ext_spamvirustest_get_score(ext, mctx->score_ratio, percent);
	return SIEVE_EXEC_OK;

failed:
	mctx->score_ratio = -1;
	*value_r = "0";
	return SIEVE_EXEC_OK;
}
