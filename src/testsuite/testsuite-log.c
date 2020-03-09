/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "array.h"

#include "sieve-common.h"
#include "sieve-stringlist.h"
#include "sieve-error-private.h"

#include "testsuite-common.h"
#include "testsuite-log.h"

/*
 * Configuration
 */

bool _testsuite_log_stdout = FALSE;

/*
 * Testsuite log error handlers
 */

struct sieve_error_handler *testsuite_log_ehandler = NULL;
struct sieve_error_handler *testsuite_log_main_ehandler = NULL;

struct _testsuite_log_message {
	const char *location;
	const char *message;
};

static pool_t _testsuite_logmsg_pool = NULL;
ARRAY(struct _testsuite_log_message) _testsuite_log_errors;
ARRAY(struct _testsuite_log_message) _testsuite_log_warnings;
ARRAY(struct _testsuite_log_message) _testsuite_log_messages;

static inline void
_testsuite_stdout_log(const struct sieve_error_params *params,
		      const char *prefix, const char *message)
{
	if (_testsuite_log_stdout) {
		if (params->location == NULL || *params->location == '\0') {
			fprintf(stdout, "LOG: %s: %s\n",
				prefix, message);
		} else {
			fprintf(stdout, "LOG: %s: %s: %s\n",
				params->location, prefix, message);
		}
	}
}

static void
_testsuite_log(struct sieve_error_handler *ehandler ATTR_UNUSED,
	       const struct sieve_error_params *params,
	       enum sieve_error_flags flags ATTR_UNUSED, const char *message)
{
	pool_t pool = _testsuite_logmsg_pool;
	struct _testsuite_log_message msg;
	const char *prefix;

	switch (params->log_type) {
	case LOG_TYPE_ERROR:
		prefix = "error";
		break;
	case LOG_TYPE_WARNING:
		prefix = "warning";
		break;
	case LOG_TYPE_INFO:
		prefix = "info";
		break;
	case LOG_TYPE_DEBUG:
		prefix = "debug";
		break;
	default:
		i_unreached();
	}

	_testsuite_stdout_log(params, prefix, message);

	msg.location = p_strdup(pool, params->location);
	msg.message = p_strdup(pool, message);

	switch (params->log_type) {
	case LOG_TYPE_ERROR:
		array_append(&_testsuite_log_errors, &msg, 1);
		break;
	case LOG_TYPE_WARNING:
		array_append(&_testsuite_log_warnings, &msg, 1);
		break;
	case LOG_TYPE_INFO:
		array_append(&_testsuite_log_messages, &msg, 1);
		break;
	case LOG_TYPE_DEBUG:
		break;
	default:
		i_unreached();
	}
}

static void
_testsuite_main_log(struct sieve_error_handler *ehandler,
		    const struct sieve_error_params *params,
		    enum sieve_error_flags flags, const char *message)
{
	if (params->log_type != LOG_TYPE_ERROR)
		return _testsuite_log(ehandler, params, flags, message);

	if (params->location == NULL || *params->location == '\0')
		fprintf(stderr, "error: %s\n", message);
	else
		fprintf(stderr, "%s: error: %s\n", params->location, message);
}

static struct sieve_error_handler *_testsuite_log_ehandler_create(void)
{
	pool_t pool;
	struct sieve_error_handler *ehandler;

	pool = pool_alloconly_create("testsuite_log_ehandler",
				     sizeof(struct sieve_error_handler));
	ehandler = p_new(pool, struct sieve_error_handler, 1);
	sieve_error_handler_init(ehandler, testsuite_sieve_instance, pool, 0);

	ehandler->log = _testsuite_log;

	return ehandler;
}

static struct sieve_error_handler *_testsuite_log_main_ehandler_create(void)
{
	pool_t pool;
	struct sieve_error_handler *ehandler;

	pool = pool_alloconly_create("testsuite_log_main_ehandler",
				     sizeof(struct sieve_error_handler));
	ehandler = p_new(pool, struct sieve_error_handler, 1);
	sieve_error_handler_init(ehandler, testsuite_sieve_instance, pool, 0);

	ehandler->log = _testsuite_main_log;

	return ehandler;
}

static void ATTR_FORMAT(2, 0)
testsuite_error_handler(const struct failure_context *ctx, const char *fmt,
			va_list args)
{
	struct sieve_error_params params = {
		.location = NULL,
	};
	pool_t pool = _testsuite_logmsg_pool;
	struct _testsuite_log_message msg;

	i_zero(&msg);
	switch (ctx->type) {
	case LOG_TYPE_DEBUG:
		T_BEGIN {
			_testsuite_stdout_log(&params, "debug",
					      t_strdup_vprintf(fmt, args));
		} T_END;
		break;
	case LOG_TYPE_INFO:
		msg.message = p_strdup_vprintf(pool, fmt, args);
		array_append(&_testsuite_log_messages, &msg, 1);

		_testsuite_stdout_log(&params, "info", msg.message);
		break;
	case LOG_TYPE_WARNING:
		msg.message = p_strdup_vprintf(pool, fmt, args);
		array_append(&_testsuite_log_warnings, &msg, 1);

		_testsuite_stdout_log(&params, "warning", msg.message);
		break;
	case LOG_TYPE_ERROR:
		msg.message = p_strdup_vprintf(pool, fmt, args);
		array_append(&_testsuite_log_errors, &msg, 1);

		_testsuite_stdout_log(&params, "error", msg.message);
		break;
	default:
		default_error_handler(ctx, fmt, args);
		break;
	}
}

/*
 *
 */

void testsuite_log_clear_messages(void)
{
	if (_testsuite_logmsg_pool != NULL) {
		if (array_count(&_testsuite_log_errors) == 0)
			return;
		pool_unref(&_testsuite_logmsg_pool);
	}

	_testsuite_logmsg_pool =
		pool_alloconly_create("testsuite_log_messages", 8192);

	p_array_init(&_testsuite_log_errors, _testsuite_logmsg_pool, 128);
	p_array_init(&_testsuite_log_warnings, _testsuite_logmsg_pool, 128);
	p_array_init(&_testsuite_log_messages, _testsuite_logmsg_pool, 128);

	sieve_error_handler_reset(testsuite_log_ehandler);
}

/*
 *
 */

void testsuite_log_init(bool log_stdout)
{
	_testsuite_log_stdout = log_stdout;

	testsuite_log_ehandler = _testsuite_log_ehandler_create();
	sieve_error_handler_accept_infolog(testsuite_log_ehandler, TRUE);
	sieve_error_handler_accept_debuglog(testsuite_log_ehandler, TRUE);

	testsuite_log_main_ehandler = _testsuite_log_main_ehandler_create();
	sieve_error_handler_accept_infolog(testsuite_log_main_ehandler, TRUE);
	sieve_error_handler_accept_debuglog(testsuite_log_main_ehandler, TRUE);

	i_set_error_handler(testsuite_error_handler);
	i_set_info_handler(testsuite_error_handler);
	i_set_debug_handler(testsuite_error_handler);

	testsuite_log_clear_messages();
}

void testsuite_log_deinit(void)
{
	sieve_error_handler_unref(&testsuite_log_ehandler);
	sieve_error_handler_unref(&testsuite_log_main_ehandler);

	i_set_error_handler(default_error_handler);
	i_set_info_handler(default_error_handler);
	i_set_debug_handler(default_error_handler);

	pool_unref(&_testsuite_logmsg_pool);
}

/*
 * Log stringlist
 */

/* Forward declarations */

static int
testsuite_log_stringlist_next_item(struct sieve_stringlist *_strlist,
				   string_t **str_r);
static void testsuite_log_stringlist_reset(struct sieve_stringlist *_strlist);

/* Stringlist object */

struct testsuite_log_stringlist {
	struct sieve_stringlist strlist;

	int pos, index;
};

struct sieve_stringlist *
testsuite_log_stringlist_create(const struct sieve_runtime_env *renv,
				int index)
{
	struct testsuite_log_stringlist *strlist;

	strlist = t_new(struct testsuite_log_stringlist, 1);
	strlist->strlist.runenv = renv;
	strlist->strlist.exec_status = SIEVE_EXEC_OK;
	strlist->strlist.next_item = testsuite_log_stringlist_next_item;
	strlist->strlist.reset = testsuite_log_stringlist_reset;

 	strlist->index = index;
	strlist->pos = 0;

	return &strlist->strlist;
}

static int
testsuite_log_stringlist_next_item(struct sieve_stringlist *_strlist,
				   string_t **str_r)
{
	struct testsuite_log_stringlist *strlist =
		(struct testsuite_log_stringlist *) _strlist;
	const struct _testsuite_log_message *msg;
	int pos;

	*str_r = NULL;

	if (strlist->pos < 0)
		return 0;

	if (strlist->index > 0) {
		pos = strlist->index - 1;
		strlist->pos = -1;
	} else {
		pos = strlist->pos++;
	}

	if (pos >= (int) array_count(&_testsuite_log_errors)) {
		strlist->pos = -1;
		return 0;
	}

	msg = array_idx(&_testsuite_log_errors, (unsigned int) pos);

	*str_r = t_str_new_const(msg->message, strlen(msg->message));
	return 1;
}

static void testsuite_log_stringlist_reset(struct sieve_stringlist *_strlist)
{
	struct testsuite_log_stringlist *strlist =
		(struct testsuite_log_stringlist *) _strlist;

	strlist->pos = 0;
}
