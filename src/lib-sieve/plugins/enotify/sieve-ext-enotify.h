#ifndef SIEVE_EXT_ENOTIFY_H
#define SIEVE_EXT_ENOTIFY_H

#include "lib.h"
#include "compat.h"
#include <stdarg.h>

#include "sieve-common.h"
#include "sieve-error.h"

/*
 * Forward declarations
 */

struct sieve_enotify_method;
struct sieve_enotify_env;
struct sieve_enotify_action;
struct sieve_enotify_print_env;
struct sieve_enotify_exec_env;

/*
 * Enotify extension
 */

int sieve_ext_enotify_get_extension(struct sieve_instance *svinst,
				    const struct sieve_extension **ext_r);
int sieve_ext_enotify_require_extension(struct sieve_instance *svinst,
					const struct sieve_extension **ext_r);

/*
 * Notify method definition
 */

struct sieve_enotify_method_def {
	const char *identifier;

	/* Registration */
	bool (*load)(const struct sieve_enotify_method *nmth, void **context);
	void (*unload)(const struct sieve_enotify_method *nmth);

	/* Validation */
	bool (*compile_check_uri)(const struct sieve_enotify_env *nenv,
				  const char *uri, const char *uri_body);
	bool (*compile_check_message)(const struct sieve_enotify_env *nenv,
				      string_t *message);
	bool (*compile_check_from)(const struct sieve_enotify_env *nenv,
				   string_t *from);
	bool (*compile_check_option)(const struct sieve_enotify_env *nenv,
				     const char *option, const char *value);

	/* Runtime */
	bool (*runtime_check_uri)(const struct sieve_enotify_env *nenv,
				  const char *uri, const char *uri_body);
	const char *(*runtime_get_method_capability)(
		const struct sieve_enotify_env *nenv, const char *uri,
		const char *uri_body, const char *capability);
	bool (*runtime_check_operands)(
		const struct sieve_enotify_env *nenv, const char *uri,
		const char *uri_body, string_t *message, string_t *from,
		pool_t context_pool, void **method_context);
	bool (*runtime_set_option)(
		const struct sieve_enotify_env *nenv, void *method_context,
		const char *option, const char *value);

	/* Action duplicates */
	int (*action_check_duplicates)(
		const struct sieve_enotify_env *nenv,
		const struct sieve_enotify_action *nact,
		const struct sieve_enotify_action *nact_other);

	/* Action print */
	void (*action_print)(
		const struct sieve_enotify_print_env *penv,
		const struct sieve_enotify_action *nact);

	/* Action execution
	   (returns 0 if all is ok and -1 for temporary error)
	 */
	int (*action_execute)(const struct sieve_enotify_exec_env *nenv,
			      const struct sieve_enotify_action *nact);
};

/*
 * Notify method instance
 */

struct sieve_enotify_method {
	const struct sieve_enotify_method_def *def;
	int id;

	struct sieve_instance *svinst;
	const struct sieve_extension *ext;
	void *context;
};

int sieve_enotify_method_register(
	const struct sieve_extension *ntfy_ext,
	const struct sieve_enotify_method_def *nmth_def,
	const struct sieve_enotify_method **nmth_r);
void  sieve_enotify_method_unregister(const struct sieve_enotify_method *nmth);

/*
 * Notify method environment
 */

struct sieve_enotify_env {
	struct sieve_instance *svinst;

	const struct sieve_enotify_method *method;

	struct sieve_error_handler *ehandler;
	const char *location;
	struct event *event;
};

/*
 * Notify method printing
 */

void sieve_enotify_method_printf(const struct sieve_enotify_print_env *penv,
				 const char *fmt, ...) ATTR_FORMAT(2, 3);

/*
 * Notify execution environment
 */

struct sieve_enotify_exec_env {
	struct sieve_instance *svinst;
	enum sieve_execute_flags flags;

	const struct sieve_enotify_method *method;

	const struct sieve_script_env *scriptenv;
	const struct sieve_message_data *msgdata;
	struct sieve_message_context *msgctx;

	struct sieve_error_handler *ehandler;
	const char *location;
	struct event *event;
};

struct event_passthrough *
sieve_enotify_create_finish_event(const struct sieve_enotify_exec_env *nenv);

/*
 * Notify action
 */

struct sieve_enotify_action {
	const struct sieve_enotify_method *method;
	void *method_context;

	sieve_number_t importance;
	const char *message;
	const char *from;
};

/*
 * Error handling
 */

#define sieve_enotify_error(ENV, ...) \
	sieve_event_log((ENV)->svinst, (ENV)->ehandler, (ENV)->event, \
			LOG_TYPE_ERROR, (ENV)->location, 0, __VA_ARGS__ )
#define sieve_enotify_warning(ENV, ...) \
	sieve_event_log((ENV)->svinst, (ENV)->ehandler, (ENV)->event, \
			LOG_TYPE_WARNING, \
			(ENV)->location, 0, __VA_ARGS__ )
#define sieve_enotify_info(ENV, ...) \
	sieve_event_log((ENV)->svinst, (ENV)->ehandler, (ENV)->event, \
			LOG_TYPE_INFO, \
			(ENV)->location, 0, __VA_ARGS__ )
#define sieve_enotify_critical(ENV, ...) \
	sieve_critical((ENV)->svinst, (ENV)->ehandler, NULL, __VA_ARGS__ )

#define sieve_enotify_global_error(ENV, ...) \
	sieve_event_log((ENV)->svinst, (ENV)->ehandler, (ENV)->event, \
			LOG_TYPE_ERROR, (ENV)->location, \
			SIEVE_ERROR_FLAG_GLOBAL, __VA_ARGS__ )
#define sieve_enotify_global_warning(ENV, ...) \
	sieve_event_log((ENV)->svinst, (ENV)->ehandler, (ENV)->event, \
			LOG_TYPE_WARNING, (ENV)->location, \
			SIEVE_ERROR_FLAG_GLOBAL, __VA_ARGS__ )
#define sieve_enotify_global_info(ENV, ...) \
	sieve_event_log((ENV)->svinst, (ENV)->ehandler, (ENV)->event, \
			LOG_TYPE_INFO, (ENV)->location, \
			SIEVE_ERROR_FLAG_GLOBAL, __VA_ARGS__ )

#define sieve_enotify_event_log(ENV, EVENT, ...) \
	sieve_event_log((ENV)->svinst, (ENV)->ehandler, (EVENT), \
			LOG_TYPE_INFO, (ENV)->location, \
			SIEVE_ERROR_FLAG_GLOBAL, __VA_ARGS__ )

#define sieve_enotify_global_log_error(ENV, ...) \
	sieve_event_log((ENV)->svinst, (ENV)->ehandler, (ENV)->event, \
			LOG_TYPE_ERROR, (ENV)->location, \
			(SIEVE_ERROR_FLAG_GLOBAL | \
			 SIEVE_ERROR_FLAG_GLOBAL_MAX_INFO), __VA_ARGS__ )
#endif
