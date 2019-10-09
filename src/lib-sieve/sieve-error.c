/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "array.h"
#include "ostream.h"
#include "var-expand.h"
#include "eacces-error.h"

#include "sieve-common.h"
#include "sieve-script.h"
#include "sieve-error-private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>

/*
 * Definitions
 */

#define CRITICAL_MSG \
	"internal error occurred: refer to server log for more information."
#define CRITICAL_MSG_STAMP CRITICAL_MSG " [%Y-%m-%d %H:%M:%S]"

/* Logfile error handler will rotate log when it exceeds 10k bytes */
#define LOGFILE_MAX_SIZE (10 * 1024)

/*
 * Utility
 */

const char *
sieve_error_script_location(const struct sieve_script *script,
			    unsigned int source_line)
{
	const char *sname;

	sname = (script == NULL ? NULL : sieve_script_name(script));

	if (sname == NULL || *sname == '\0') {
		if (source_line == 0)
			return NULL;

		return t_strdup_printf("line %d", source_line);
	}

	if (source_line == 0)
		return sname;

	return t_strdup_printf("%s: line %d", sname, source_line);
}

const char *sieve_error_from_external(const char *msg)
{
	char *new_msg;

	if (msg == NULL || *msg == '\0')
		return msg;

	new_msg = t_strdup_noconst(msg);
	new_msg[0] = i_tolower(new_msg[0]);

	return new_msg;
}

/*
 * Initialization
 */

void sieve_errors_init(struct sieve_instance *svinst ATTR_UNUSED)
{
	/* nothing */
}

void sieve_errors_deinit(struct sieve_instance *svinst ATTR_UNUSED)
{
	/* nothing */
}

/*
 * Direct handler calls
 */

static void
sieve_direct_master_log(struct sieve_instance *svinst,
			const struct sieve_error_params *params,
			const char *message)
{
	struct event_log_params event_params = {
		.log_type = params->log_type,
		.source_filename = params->csrc.filename,
		.source_linenum = params->csrc.linenum,

		.base_event = svinst->event,
	};
	struct event *event = (params->event != NULL ?
			       params->event : svinst->event);

	if (params->location != NULL && *params->location != '\0') {
		event_params.base_send_prefix =
			 t_strconcat(params->location, ": ", NULL);
	}

	event_log(event, &event_params, "%s", message);
}

void sieve_direct_logv(struct sieve_instance *svinst,
		       struct sieve_error_handler *ehandler,
		       const struct sieve_error_params *params,
		       enum sieve_error_flags flags,
		       const char *fmt, va_list args)
{
	struct event_log_params event_params = {
		.log_type = params->log_type,
		.source_filename = params->csrc.filename,
		.source_linenum = params->csrc.linenum,
		.base_event = svinst->event,
		.base_str_out = NULL,
		.no_send = TRUE,
	};
	struct event *event = (params->event != NULL ?
			       params->event : svinst->event);
	bool event_log = FALSE, ehandler_log = FALSE;

	if (ehandler != NULL) {
		switch (params->log_type) {
		case LOG_TYPE_ERROR:
			ehandler_log = sieve_errors_more_allowed(ehandler);
			break;
		case LOG_TYPE_WARNING:
			ehandler_log = TRUE;
			break;
		case LOG_TYPE_INFO:
			ehandler_log = ehandler->log_info;
			break;
		case LOG_TYPE_DEBUG:
			ehandler_log = ehandler->log_debug;
			break;
		case LOG_TYPE_FATAL:
		case LOG_TYPE_PANIC:
		case LOG_TYPE_COUNT:
		case LOG_TYPE_OPTION:
			i_unreached();
		}
	}

	if (ehandler != NULL && ehandler->master_log) {
		event_log = ehandler_log;
		ehandler_log = FALSE;
	}
	if ((flags & SIEVE_ERROR_FLAG_GLOBAL) != 0) {
		event_log = TRUE;
		if ((flags & SIEVE_ERROR_FLAG_GLOBAL_MAX_INFO) != 0 &&
		    params->log_type > LOG_TYPE_INFO)
			event_params.log_type = LOG_TYPE_INFO;
	}

	if (event_log) {
		event_params.no_send = FALSE;
		if (params->location != NULL && *params->location != '\0') {
			event_params.base_send_prefix =
				t_strconcat(params->location, ": ", NULL);
		}
	}
	if (ehandler_log) {
		if (ehandler->log == NULL)
			ehandler_log = FALSE;
		else
			event_params.base_str_out = t_str_new(128);
	}

	if (event_log || ehandler_log)
		event_logv(event, &event_params, fmt, args);

	if (ehandler_log) {
		ehandler->log(ehandler, params, flags,
			      str_c(event_params.base_str_out));
	}

	if (ehandler != NULL && ehandler->pool != NULL) {
		switch (params->log_type) {
		case LOG_TYPE_ERROR:
			ehandler->errors++;
			break;
		case LOG_TYPE_WARNING:
			ehandler->warnings++;
			break;
		default:
			break;
		}
	}
}

/*
 * User errors
 */

void sieve_global_logv(struct sieve_instance *svinst,
		       struct sieve_error_handler *ehandler,
		       const struct sieve_error_params *params,
		       const char *fmt, va_list args)
{
	sieve_direct_logv(svinst, ehandler, params,
			  SIEVE_ERROR_FLAG_GLOBAL, fmt, args);
}

void sieve_global_info_logv(struct sieve_instance *svinst,
			    struct sieve_error_handler *ehandler,
			    const struct sieve_error_params *params,
			    const char *fmt, va_list args)
{
	sieve_direct_logv(svinst, ehandler, params,
			  (SIEVE_ERROR_FLAG_GLOBAL |
			   SIEVE_ERROR_FLAG_GLOBAL_MAX_INFO), fmt, args);
}

#undef sieve_global_error
void sieve_global_error(struct sieve_instance *svinst,
			struct sieve_error_handler *ehandler,
			const char *csrc_filename, unsigned int csrc_linenum,
			const char *location, const char *fmt, ...)
{
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_ERROR,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
		.location = location,
	};
	va_list args;
	va_start(args, fmt);

	T_BEGIN {
		sieve_global_logv(svinst, ehandler, &params, fmt, args);
	} T_END;

	va_end(args);
}

#undef sieve_global_warning
void sieve_global_warning(struct sieve_instance *svinst,
			  struct sieve_error_handler *ehandler,
			  const char *csrc_filename, unsigned int csrc_linenum,
			  const char *location, const char *fmt, ...)
{
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_WARNING,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
		.location = location,
	};
	va_list args;
	va_start(args, fmt);

	T_BEGIN {
		sieve_global_logv(svinst, ehandler, &params, fmt, args);
	} T_END;

	va_end(args);
}

#undef sieve_global_info
void sieve_global_info(struct sieve_instance *svinst,
		       struct sieve_error_handler *ehandler,
		       const char *csrc_filename, unsigned int csrc_linenum,
		       const char *location, const char *fmt, ...)
{
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_INFO,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
		.location = location,
	};
	va_list args;
	va_start(args, fmt);

	T_BEGIN {
		sieve_global_logv(svinst, ehandler, &params, fmt, args);
	} T_END;

	va_end(args);
}

#undef sieve_global_info_error
void sieve_global_info_error(struct sieve_instance *svinst,
			     struct sieve_error_handler *ehandler,
			     const char *csrc_filename,
			     unsigned int csrc_linenum,
			     const char *location, const char *fmt, ...)
{
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_ERROR,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
		.location = location,
	};
	va_list args;
	va_start(args, fmt);

	T_BEGIN {
		sieve_global_info_logv(svinst, ehandler, &params, fmt, args);
	} T_END;

	va_end(args);
}

#undef sieve_global_info_warning
void sieve_global_info_warning(struct sieve_instance *svinst,
			       struct sieve_error_handler *ehandler,
			       const char *csrc_filename,
			       unsigned int csrc_linenum,
			       const char *location, const char *fmt, ...)
{
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_WARNING,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
		.location = location,
	};
	va_list args;
	va_start(args, fmt);

	T_BEGIN {
		sieve_global_info_logv(svinst, ehandler, &params, fmt, args);
	} T_END;

	va_end(args);
}

/*
 * Default (user) error functions
 */

void sieve_internal_error_params(struct sieve_error_handler *ehandler,
				 const struct sieve_error_params *params,
				 const char *user_prefix)
{
	char str[256];
	const char *msg;
	struct tm *tm;

	if (ehandler == NULL || ehandler->master_log)
		return;

	tm = localtime(&ioloop_time);
	msg = (strftime(str, sizeof(str), CRITICAL_MSG_STAMP, tm) > 0 ?
	       str : CRITICAL_MSG);

	if (user_prefix == NULL || *user_prefix == '\0') {
		sieve_direct_log(ehandler->svinst, ehandler, params, 0,
				 "%s", msg);
	} else {
		sieve_direct_log(ehandler->svinst, ehandler, params, 0,
				 "%s: %s", user_prefix, msg);
	}
}

#undef sieve_internal_error
void sieve_internal_error(struct sieve_error_handler *ehandler,
			  const char *csrc_filename, unsigned int csrc_linenum,
			  const char *location, const char *user_prefix)

{
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_ERROR,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
		.location = location,
	};

	sieve_internal_error_params(ehandler, &params, user_prefix);
}

void sieve_logv(struct sieve_error_handler *ehandler,
		const struct sieve_error_params *params,
		const char *fmt, va_list args)
{
	if (ehandler == NULL) return;

	sieve_direct_logv(ehandler->svinst, ehandler, params, 0, fmt, args);
}

void sieve_event_logv(struct sieve_instance *svinst,
		      struct sieve_error_handler *ehandler,
		      struct event *event,  enum log_type log_type,
		      const char *csrc_filename, unsigned int csrc_linenum,
		      const char *location, enum sieve_error_flags flags,
		      const char *fmt, va_list args)
{
	struct sieve_error_params params = {
		.log_type = log_type,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
		.event = event,
		.location = location,
	};

	T_BEGIN {
		sieve_direct_logv(svinst, ehandler, &params, flags, fmt, args);
	} T_END;
}


#undef sieve_event_log
void sieve_event_log(struct sieve_instance *svinst,
		     struct sieve_error_handler *ehandler,
		     struct event *event,  enum log_type log_type,
		     const char *csrc_filename, unsigned int csrc_linenum,
		     const char *location, enum sieve_error_flags flags,
		     const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	sieve_event_logv(svinst, ehandler, event, log_type, csrc_filename,
			 csrc_linenum, location, flags, fmt, args);

	va_end(args);
}

void sieve_criticalv(struct sieve_instance *svinst,
		     struct sieve_error_handler *ehandler,
		     const struct sieve_error_params *params,
		     const char *user_prefix, const char *fmt, va_list args)
{
	struct sieve_error_params new_params = *params;

	new_params.log_type = LOG_TYPE_ERROR;

	sieve_direct_master_log(svinst, &new_params,
				t_strdup_vprintf(fmt, args));
	sieve_internal_error_params(ehandler, &new_params, user_prefix);
}

#undef sieve_error
void sieve_error(struct sieve_error_handler *ehandler,
		 const char *csrc_filename, unsigned int csrc_linenum,
		 const char *location, const char *fmt, ...)
{
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_ERROR,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
		.location = location,
	};
	va_list args;
	va_start(args, fmt);

	T_BEGIN {
		sieve_logv(ehandler, &params, fmt, args);
	} T_END;

	va_end(args);
}

#undef sieve_warning
void sieve_warning(struct sieve_error_handler *ehandler,
		   const char *csrc_filename, unsigned int csrc_linenum,
		   const char *location, const char *fmt, ...)
{
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_WARNING,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
		.location = location,
	};
	va_list args;
	va_start(args, fmt);

	T_BEGIN {
		sieve_logv(ehandler, &params, fmt, args);
	} T_END;

	va_end(args);
}

#undef sieve_info
void sieve_info(struct sieve_error_handler *ehandler,
		const char *csrc_filename, unsigned int csrc_linenum,
		const char *location, const char *fmt, ...)
{
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_INFO,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
		.location = location,
	};
	va_list args;
	va_start(args, fmt);

	T_BEGIN {
		sieve_logv(ehandler, &params, fmt, args);
	} T_END;

	va_end(args);
}

#undef sieve_debug
void sieve_debug(struct sieve_error_handler *ehandler,
		 const char *csrc_filename, unsigned int csrc_linenum,
		 const char *location, const char *fmt, ...)
{
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_DEBUG,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
		.location = location,
	};
	va_list args;
	va_start(args, fmt);

	T_BEGIN {
		sieve_logv(ehandler, &params, fmt, args);
	} T_END;

	va_end(args);
}

#undef sieve_critical
void sieve_critical(struct sieve_instance *svinst,
		    struct sieve_error_handler *ehandler,
		    const char *csrc_filename, unsigned int csrc_linenum,
		    const char *location, const char *user_prefix,
		    const char *fmt, ...)
{
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_ERROR,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
		.location = location,
	};
	va_list args;

	va_start(args, fmt);

	T_BEGIN {
		sieve_criticalv(svinst, ehandler, &params, user_prefix,
				fmt, args);
	} T_END;

	va_end(args);
}

/*
 * Error statistics
 */

unsigned int sieve_get_errors(struct sieve_error_handler *ehandler)
{
	if (ehandler == NULL || ehandler->pool == NULL)
		return 0;

	return ehandler->errors;
}

unsigned int sieve_get_warnings(struct sieve_error_handler *ehandler)
{
	if (ehandler == NULL || ehandler->pool == NULL)
		return 0;

	return ehandler->warnings;
}

bool sieve_errors_more_allowed(struct sieve_error_handler *ehandler)
{
	if (ehandler == NULL || ehandler->pool == NULL)
		return TRUE;

	return (ehandler->max_errors == 0 ||
		ehandler->errors < ehandler->max_errors);
}

/*
 * Error handler configuration
 */

void sieve_error_handler_accept_infolog(struct sieve_error_handler *ehandler,
					bool enable)
{
	ehandler->log_info = enable;
}

void sieve_error_handler_accept_debuglog(struct sieve_error_handler *ehandler,
					 bool enable)
{
	ehandler->log_debug = enable;
}

/*
 * Error handler init
 */

void sieve_error_handler_init(struct sieve_error_handler *ehandler,
			      struct sieve_instance *svinst,
			      pool_t pool, unsigned int max_errors)
{
	ehandler->pool = pool;
	ehandler->svinst = svinst;
	ehandler->refcount = 1;
	ehandler->max_errors = max_errors;

	ehandler->errors = 0;
	ehandler->warnings = 0;
}

void sieve_error_handler_ref(struct sieve_error_handler *ehandler)
{
	if (ehandler == NULL || ehandler->pool == NULL)
		return;

	ehandler->refcount++;
}

void sieve_error_handler_unref(struct sieve_error_handler **ehandler)
{
	if (*ehandler == NULL || (*ehandler)->pool == NULL)
		return;

	i_assert((*ehandler)->refcount > 0);

	if (--(*ehandler)->refcount != 0)
        	return;

	if ((*ehandler)->free != NULL)
		(*ehandler)->free(*ehandler);

	pool_unref(&((*ehandler)->pool));
	*ehandler = NULL;
}

void sieve_error_handler_reset(struct sieve_error_handler *ehandler)
{
	if (ehandler == NULL || ehandler->pool == NULL)
		return;

	ehandler->errors = 0;
	ehandler->warnings = 0;
}

/*
 * Error params utility
 */

static void
sieve_error_params_add_prefix(struct sieve_error_handler *ehandler ATTR_UNUSED,
			      const struct sieve_error_params *params,
			      string_t *prefix)
{
	if (params->location != NULL && *params->location != '\0') {
		str_append(prefix, params->location);
		str_append(prefix, ": ");
	}

	switch (params->log_type) {
	case LOG_TYPE_ERROR:
		str_append(prefix, "error: ");
		break;
	case LOG_TYPE_WARNING:
		str_append(prefix, "warning: ");
		break;
	case LOG_TYPE_INFO:
		str_append(prefix, "info: ");
		break;
	case LOG_TYPE_DEBUG:
		str_append(prefix, "debug: ");
		break;
	default:
		i_unreached();
	}
}

/*
 * Master/System error handler
 *
 * - Output errors directly to Dovecot master log
 */

struct sieve_error_handler *
sieve_master_ehandler_create(struct sieve_instance *svinst,
			     unsigned int max_errors)
{
	struct sieve_error_handler *ehandler;
	pool_t pool;

	pool = pool_alloconly_create("master_error_handler", 256);
	ehandler = p_new(pool, struct sieve_error_handler, 1);
	sieve_error_handler_init(ehandler, svinst, pool, max_errors);
	ehandler->master_log = TRUE;
	ehandler->log_debug = svinst->debug;

	return ehandler;
}

/*
 * STDERR error handler
 *
 * - Output errors directly to stderror
 */

static void
sieve_stderr_log(struct sieve_error_handler *ehandler,
		 const struct sieve_error_params *params,
		 enum sieve_error_flags flags ATTR_UNUSED,
		 const char *message)
{
	string_t *prefix = t_str_new(64);

	sieve_error_params_add_prefix(ehandler, params, prefix);

	fprintf(stderr, "%s%s.\n", str_c(prefix), message);
}

struct sieve_error_handler *
sieve_stderr_ehandler_create(struct sieve_instance *svinst,
			     unsigned int max_errors)
{
	pool_t pool;
	struct sieve_error_handler *ehandler;

	/* Pool is not strictly necessary, but other handler types will need
	 * a pool, so this one will have one too.
	 */
	pool = pool_alloconly_create("stderr_error_handler",
				     sizeof(struct sieve_error_handler));
	ehandler = p_new(pool, struct sieve_error_handler, 1);
	sieve_error_handler_init(ehandler, svinst, pool, max_errors);

	ehandler->log = sieve_stderr_log;

	return ehandler;
}

/* String buffer error handler
 *
 * - Output errors to a string buffer
 */

struct sieve_strbuf_ehandler {
	struct sieve_error_handler handler;

	string_t *errors;
	bool crlf;
};

static void
sieve_strbuf_log(struct sieve_error_handler *ehandler,
		 const struct sieve_error_params *params,
		 enum sieve_error_flags flags ATTR_UNUSED, const char *message)
{
	struct sieve_strbuf_ehandler *handler =
		(struct sieve_strbuf_ehandler *) ehandler;

	sieve_error_params_add_prefix(ehandler, params, handler->errors);
	str_append(handler->errors, message);

	if (!handler->crlf)
		str_append(handler->errors, ".\n");
	else
		str_append(handler->errors, ".\r\n");
}

struct sieve_error_handler *
sieve_strbuf_ehandler_create(struct sieve_instance *svinst, string_t *strbuf,
			     bool crlf, unsigned int max_errors)
{
	pool_t pool;
	struct sieve_strbuf_ehandler *ehandler;

	pool = pool_alloconly_create("strbuf_error_handler", 256);
	ehandler = p_new(pool, struct sieve_strbuf_ehandler, 1);
	ehandler->errors = strbuf;

	sieve_error_handler_init(&ehandler->handler, svinst, pool, max_errors);

	ehandler->handler.log = sieve_strbuf_log;
	ehandler->crlf = crlf;

	return &(ehandler->handler);
}

/*
 * Logfile error handler
 *
 * - Output errors to a log file
 */

struct sieve_logfile_ehandler {
	struct sieve_error_handler handler;

	const char *logfile;
	bool started;
	int fd;
	struct ostream *stream;
};

static void
sieve_logfile_write(struct sieve_logfile_ehandler *ehandler,
		    const struct sieve_error_params *params,
		    const char *message)
{
	string_t *outbuf;
	ssize_t ret = 0, remain;
	const char *data;

	if (ehandler->stream == NULL)
		return;

	T_BEGIN {
		outbuf = t_str_new(256);
		sieve_error_params_add_prefix(&ehandler->handler,
					      params, outbuf);
		str_append(outbuf, message);
		str_append(outbuf, ".\n");

		remain = str_len(outbuf);
		data = (const char *) str_data(outbuf);

		while (remain > 0) {
			if ((ret = o_stream_send(ehandler->stream,
						 data, remain)) < 0)
				break;

			remain -= ret;
			data += ret;
		}
	} T_END;

	if (ret < 0) {
		e_error(ehandler->handler.svinst->event,
			"o_stream_send() failed on logfile %s: %m",
			ehandler->logfile);
	}
}

inline static void ATTR_FORMAT(5, 6)
sieve_logfile_printf(struct sieve_logfile_ehandler *ehandler,
		     const char *csrc_filename, unsigned int csrc_linenum,
		     const char *location, const char *fmt, ...)
{
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_INFO,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
		.location = location,
	};
	va_list args;
	va_start(args, fmt);

	sieve_logfile_write(ehandler, &params, t_strdup_vprintf(fmt, args));

	va_end(args);
}

static void sieve_logfile_start(struct sieve_logfile_ehandler *ehandler)
{
	struct sieve_instance *svinst = ehandler->handler.svinst;
	struct ostream *ostream = NULL;
	struct stat st;
	struct tm *tm;
	char buf[256];
	time_t now;
	int fd;

	/* Open the logfile */

	fd = open(ehandler->logfile, O_CREAT | O_APPEND | O_WRONLY, 0600);
	if (fd == -1) {
		if (errno == EACCES) {
			e_error(svinst->event,
				"failed to open logfile "
				"(LOGGING TO STDERR): %s",
				eacces_error_get_creating("open",
							  ehandler->logfile));
		} else {
			e_error(svinst->event, "failed to open logfile "
				"(LOGGING TO STDERR): "
				"open(%s) failed: %m", ehandler->logfile);
		}
		fd = STDERR_FILENO;
	} else {
		/* fd_close_on_exec(fd, TRUE); Necessary? */

		/* Stat the log file to obtain size information */
		if (fstat(fd, &st) != 0) {
			e_error(svinst->event, "failed to stat logfile "
				"(logging to STDERR): "
				"fstat(fd=%s) failed: %m", ehandler->logfile);

			if (close(fd) < 0) {
				e_error(svinst->event,
					"failed to close logfile after error: "
					"close(fd=%s) failed: %m",
					ehandler->logfile);
			}

			fd = STDERR_FILENO;
		}

		/* Rotate log when it has grown too large */
		if (st.st_size >= LOGFILE_MAX_SIZE) {
			const char *rotated;

			/* Close open file */
			if (close(fd) < 0) {
				e_error(svinst->event,
					"failed to close logfile: "
					"close(fd=%s) failed: %m",
					ehandler->logfile);
			}

			/* Rotate logfile */
			rotated = t_strconcat(ehandler->logfile, ".0", NULL);
			if (rename(ehandler->logfile, rotated) < 0 &&
			    errno != ENOENT) {
				if (errno == EACCES) {
					const char *target =
						t_strconcat(ehandler->logfile,
							    ", ", rotated, NULL);
					e_error(svinst->event,
						"failed to rotate logfile: %s",
						eacces_error_get_creating(
							"rename", target));
				} else {
					e_error(svinst->event,
						"failed to rotate logfile: "
						"rename(%s, %s) failed: %m",
						ehandler->logfile, rotated);
				}
			}

			/* Open clean logfile (overwrites existing if rename() failed earlier) */
			fd = open(ehandler->logfile,
				O_CREAT | O_APPEND | O_WRONLY | O_TRUNC, 0600);
			if (fd == -1) {
				if (errno == EACCES) {
					e_error(svinst->event,
						"failed to open logfile "
						"(LOGGING TO STDERR): %s",
						eacces_error_get_creating(
							"open", ehandler->logfile));
				} else {
					e_error(svinst->event,
						"failed to open logfile "
						"(LOGGING TO STDERR): "
						"open(%s) failed: %m",
						ehandler->logfile);
				}
				fd = STDERR_FILENO;
			}
		}
	}

	ostream = o_stream_create_fd(fd, 0);
	if (ostream == NULL) {
		/* Can't we do anything else in this most awkward situation? */
		e_error(svinst->event,
			"failed to open log stream on open file: "
			"o_stream_create_fd(fd=%s) failed "
			"(non-critical messages are not logged!)",
			ehandler->logfile);
	}

	ehandler->fd = fd;
	ehandler->stream = ostream;
	ehandler->started = TRUE;

	if (ostream != NULL) {
		now = time(NULL);
		tm = localtime(&now);

		if (strftime(buf, sizeof(buf), "%b %d %H:%M:%S", tm) > 0) {
			sieve_logfile_printf(ehandler, __FILE__, __LINE__,
					     "sieve", "started log at %s", buf);
		}
	}
}

static void
sieve_logfile_log(struct sieve_error_handler *ehandler,
		  const struct sieve_error_params *params,
		  enum sieve_error_flags flags ATTR_UNUSED,
		  const char *message)
{
	struct sieve_logfile_ehandler *handler =
		(struct sieve_logfile_ehandler *) ehandler;

	if (!handler->started)
		sieve_logfile_start(handler);

	sieve_logfile_write(handler, params, message);
}

static void sieve_logfile_free(struct sieve_error_handler *ehandler)
{
	struct sieve_logfile_ehandler *handler =
		(struct sieve_logfile_ehandler *) ehandler;

	if (handler->stream != NULL) {
		o_stream_destroy(&(handler->stream));
		if (handler->fd != STDERR_FILENO) {
			if (close(handler->fd) < 0) {
				e_error(ehandler->svinst->event,
					"failed to close logfile: "
					"close(fd=%s) failed: %m",
					handler->logfile);
			}
		}
	}
}

struct sieve_error_handler *
sieve_logfile_ehandler_create(struct sieve_instance *svinst,
			      const char *logfile, unsigned int max_errors)
{
	pool_t pool;
	struct sieve_logfile_ehandler *ehandler;

	pool = pool_alloconly_create("logfile_error_handler", 512);
	ehandler = p_new(pool, struct sieve_logfile_ehandler, 1);
	sieve_error_handler_init(&ehandler->handler, svinst, pool, max_errors);

	ehandler->handler.log = sieve_logfile_log;
	ehandler->handler.free = sieve_logfile_free;

	/* Don't open logfile until something is actually logged.
	 * Let's not pullute the sieve directory with useless logfiles.
	 */
	ehandler->logfile = p_strdup(pool, logfile);
	ehandler->started = FALSE;
	ehandler->stream = NULL;
	ehandler->fd = -1;

	return &(ehandler->handler);
}
