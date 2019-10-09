#ifndef SIEVE_ERROR_H
#define SIEVE_ERROR_H

#include "lib.h"
#include "compat.h"

#include <stdarg.h>

/*
 * Forward declarations
 */

struct var_expand_table;

struct sieve_instance;
struct sieve_script;
struct sieve_error_handler;

/*
 * Types
 */

enum sieve_error_flags {
	SIEVE_ERROR_FLAG_GLOBAL = (1 << 0),
	SIEVE_ERROR_FLAG_GLOBAL_MAX_INFO = (1 << 1),
};

struct sieve_error_params {
	enum log_type log_type;
	struct event *event;

	/* Location log command in C source code */
	struct {
		const char *filename;
		unsigned int linenum;
	} csrc;

	/* Location in Sieve source script */
	const char *location;
};

/*
 * Utility
 */

/* Converts external messages to a style that better matches Sieve user errors
 */
const char *sieve_error_from_external(const char *msg);

/*
 * Global (user+system) errors
 */

void sieve_global_logv(struct sieve_instance *svinst,
		       struct sieve_error_handler *ehandler,
		       const struct sieve_error_params *params,
		       const char *fmt, va_list args) ATTR_FORMAT(4, 0);
void sieve_global_info_logv(struct sieve_instance *svinst,
			    struct sieve_error_handler *ehandler,
			    const struct sieve_error_params *params,
			    const char *fmt, va_list args) ATTR_FORMAT(4, 0);

void sieve_global_error(struct sieve_instance *svinst,
			struct sieve_error_handler *ehandler,
			const char *csrc_filename,
			unsigned int csrc_linenum,
			const char *location, const char *fmt, ...)
			ATTR_FORMAT(6, 7);
#define sieve_global_error(svinst, ehandler, ...) \
	sieve_global_error(svinst, ehandler, __FILE__, __LINE__, __VA_ARGS__)
void sieve_global_warning(struct sieve_instance *svinst,
			  struct sieve_error_handler *ehandler,
			  const char *csrc_filename,
			  unsigned int csrc_linenum,
			  const char *location, const char *fmt, ...)
			  ATTR_FORMAT(6, 7);
#define sieve_global_warning(svinst, ehandler, ...) \
	sieve_global_warning(svinst, ehandler, __FILE__, __LINE__, __VA_ARGS__)
void sieve_global_info(struct sieve_instance *svinst,
		       struct sieve_error_handler *ehandler,
		       const char *csrc_filename,
		       unsigned int csrc_linenum,
		       const char *location, const char *fmt, ...)
		       ATTR_FORMAT(6, 7);
#define sieve_global_info(svinst, ehandler, ...) \
	sieve_global_info(svinst, ehandler, __FILE__, __LINE__, __VA_ARGS__)
void sieve_global_info_error(struct sieve_instance *svinst,
			     struct sieve_error_handler *ehandler,
			     const char *csrc_filename,
			     unsigned int csrc_linenum,
			     const char *location, const char *fmt, ...)
			     ATTR_FORMAT(6, 7);
#define sieve_global_info_error(svinst, ehandler, ...) \
	sieve_global_info_error(svinst, ehandler, __FILE__, __LINE__, \
				__VA_ARGS__)
void sieve_global_info_warning(struct sieve_instance *svinst,
			       struct sieve_error_handler *ehandler,
			       const char *csrc_filename,
			       unsigned int csrc_linenum,
			       const char *location, const char *fmt, ...)
			       ATTR_FORMAT(6, 7);
#define sieve_global_info_warning(svinst, ehandler, ...) \
	sieve_global_info_warning(svinst, ehandler, __FILE__, __LINE__, \
				  __VA_ARGS__)

/*
 * Main (user) error functions
 */

/* For these functions it is the responsibility of the caller to
 * manage the datastack.
 */

const char *
sieve_error_script_location(const struct sieve_script *script,
			    unsigned int source_line);

void sieve_logv(struct sieve_error_handler *ehandler,
		const struct sieve_error_params *params,
		const char *fmt, va_list args) ATTR_FORMAT(3, 0);

void sieve_event_logv(struct sieve_instance *svinst,
		      struct sieve_error_handler *ehandler,
		      struct event *event, enum log_type log_type,
		      const char *csrc_filename, unsigned int csrc_linenum,
		      const char *location, enum sieve_error_flags flags,
		      const char *fmt, va_list args) ATTR_FORMAT(9, 0);
void sieve_event_log(struct sieve_instance *svinst,
		     struct sieve_error_handler *ehandler,
		     struct event *event, enum log_type log_type,
		     const char *csrc_filename, unsigned int csrc_linenum,
		     const char *location, enum sieve_error_flags flags,
		     const char *fmt, ...) ATTR_FORMAT(9, 10);
#define sieve_event_log(svinst, ehandler, event, log_type, ...) \
	sieve_event_log(svinst, ehandler, event, log_type, __FILE__, __LINE__, \
			__VA_ARGS__)

void sieve_criticalv(struct sieve_instance *svinst,
		     struct sieve_error_handler *ehandler,
		     const struct sieve_error_params *params,
		     const char *user_prefix, const char *fmt, va_list args)
		     ATTR_FORMAT(5, 0);

void sieve_error(struct sieve_error_handler *ehandler,
		 const char *csrc_filename, unsigned int csrc_linenum,
		 const char *location, const char *fmt, ...) ATTR_FORMAT(5, 6);
#define sieve_error(ehandler, ...) \
	sieve_error(ehandler, __FILE__, __LINE__, __VA_ARGS__)
void sieve_warning(struct sieve_error_handler *ehandler,
		   const char *csrc_filename, unsigned int csrc_linenum,
		   const char *location, const char *fmt, ...)
		   ATTR_FORMAT(5, 6);
#define sieve_warning(ehandler, ...) \
	sieve_warning(ehandler, __FILE__, __LINE__, __VA_ARGS__)
void sieve_info(struct sieve_error_handler *ehandler,
		const char *csrc_filename, unsigned int csrc_linenum,
		const char *location, const char *fmt, ...) ATTR_FORMAT(5, 6);
#define sieve_info(ehandler, ...) \
	sieve_info(ehandler, __FILE__, __LINE__, __VA_ARGS__)
void sieve_debug(struct sieve_error_handler *ehandler,
		 const char *csrc_filename, unsigned int csrc_linenum,
		 const char *location, const char *fmt, ...) ATTR_FORMAT(5, 6);
#define sieve_debug(ehandler, ...) \
	sieve_debug(ehandler, __FILE__, __LINE__, __VA_ARGS__)
void sieve_critical(struct sieve_instance *svinst,
		    struct sieve_error_handler *ehandler,
		    const char *csrc_filename, unsigned int csrc_linenum,
		    const char *location, const char *user_prefix,
		    const char *fmt, ...) ATTR_FORMAT(7, 8);
#define sieve_critical(svinst, ehandler, ...) \
	sieve_critical(svinst, ehandler, __FILE__, __LINE__, __VA_ARGS__)


void sieve_internal_error_params(struct sieve_error_handler *ehandler,
				 const struct sieve_error_params *params,
				 const char *user_prefix);
void sieve_internal_error(struct sieve_error_handler *ehandler,
			  const char *csrc_filename, unsigned int csrc_linenum,
			  const char *location, const char *user_prefix)
			  ATTR_NULL(1, 4, 5);
#define sieve_internal_error(ehandler, ...) \
	sieve_internal_error(ehandler, __FILE__, __LINE__, __VA_ARGS__)

/*
 * Error handler configuration
 */

void sieve_error_handler_accept_infolog(struct sieve_error_handler *ehandler,
					bool enable);
void sieve_error_handler_accept_debuglog(struct sieve_error_handler *ehandler,
					 bool enable);

/*
 * Error handler statistics
 */

unsigned int sieve_get_errors(struct sieve_error_handler *ehandler);
unsigned int sieve_get_warnings(struct sieve_error_handler *ehandler);

bool sieve_errors_more_allowed(struct sieve_error_handler *ehandler);

/*
 * Error handler object
 */

void sieve_error_handler_ref(struct sieve_error_handler *ehandler);
void sieve_error_handler_unref(struct sieve_error_handler **ehandler);

void sieve_error_handler_reset(struct sieve_error_handler *ehandler);

/*
 * Error handlers
 */

/* Write errors to dovecot master log */
struct sieve_error_handler *
sieve_master_ehandler_create(struct sieve_instance *svinst,
			     unsigned int max_errors);

/* Write errors to stderr */
struct sieve_error_handler *
sieve_stderr_ehandler_create(struct sieve_instance *svinst,
			     unsigned int max_errors);

/* Write errors into a string buffer */
struct sieve_error_handler *
sieve_strbuf_ehandler_create(struct sieve_instance *svinst, string_t *strbuf,
			     bool crlf, unsigned int max_errors);

/* Write errors to a logfile */
struct sieve_error_handler *
sieve_logfile_ehandler_create(struct sieve_instance *svinst,
			      const char *logfile, unsigned int max_errors);

#endif
