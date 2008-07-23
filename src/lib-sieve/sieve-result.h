#ifndef __SIEVE_RESULT_H
#define __SIEVE_RESULT_H

#include "sieve-common.h"

struct sieve_result;
struct sieve_result_action;
struct sieve_side_effects_list;

struct sieve_result *sieve_result_create
	(struct sieve_error_handler *ehandler);
void sieve_result_ref(struct sieve_result *result); 
void sieve_result_unref(struct sieve_result **result); 
pool_t sieve_result_pool(struct sieve_result *result);

void sieve_result_extension_set_context
	(struct sieve_result *result, const struct sieve_extension *ext,
		void *context);
const void *sieve_result_extension_get_context
	(struct sieve_result *result, const struct sieve_extension *ext); 

/* Printing */

struct sieve_result_print_env {
	struct sieve_result *result;
	struct ostream *stream;
};

void sieve_result_printf
	(const struct sieve_result_print_env *penv, const char *fmt, ...);
void sieve_result_action_printf
	(const struct sieve_result_print_env *penv, const char *fmt, ...);
void sieve_result_seffect_printf
	(const struct sieve_result_print_env *penv, const char *fmt, ...);

bool sieve_result_print(struct sieve_result *result, struct ostream *stream);

/* Error handling */

void sieve_result_log
	(const struct sieve_action_exec_env *aenv, const char *fmt, ...)
		ATTR_FORMAT(2, 3);
void sieve_result_error
	(const struct sieve_action_exec_env *aenv, const char *fmt, ...)
		ATTR_FORMAT(2, 3);

void sieve_result_add_implicit_side_effect
(struct sieve_result *result, const struct sieve_action *to_action, 
	const struct sieve_side_effect *seffect, void *context);
	
int sieve_result_add_action
(const struct sieve_runtime_env *renv,
	const struct sieve_action *action, struct sieve_side_effects_list *seffects,
	unsigned int source_line, void *context);

void sieve_result_cancel_implicit_keep(struct sieve_result *result);

int sieve_result_execute
	(struct sieve_result *result, const struct sieve_message_data *msgdata,
		const struct sieve_script_env *senv);
		
struct sieve_side_effects_list *sieve_side_effects_list_create
	(struct sieve_result *result);
void sieve_side_effects_list_add
(struct sieve_side_effects_list *list, const struct sieve_side_effect *seffect, 
	void *context);

#endif
