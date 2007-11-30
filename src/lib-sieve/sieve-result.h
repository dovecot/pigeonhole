#ifndef __SIEVE_RESULT_H
#define __SIEVE_RESULT_H

#include "sieve-common.h"

struct sieve_result;
struct sieve_result_action;
struct sieve_side_effects_list;

struct sieve_result *sieve_result_create(void);
void sieve_result_ref(struct sieve_result *result); 
void sieve_result_unref(struct sieve_result **result); 
inline pool_t sieve_result_pool(struct sieve_result *result);

void sieve_result_log
	(const struct sieve_action_exec_env *aenv, const char *fmt, ...);
void sieve_result_error
	(const struct sieve_action_exec_env *aenv, const char *fmt, ...);
	
bool sieve_result_add_action
(const struct sieve_runtime_env *renv,
	const struct sieve_action *action, struct sieve_side_effects_list *seffects,
	void *context);

bool sieve_result_print(struct sieve_result *result);

void sieve_result_cancel_implicit_keep(struct sieve_result *result);

bool sieve_result_execute
	(struct sieve_result *result, const struct sieve_message_data *msgdata,
		const struct sieve_mail_environment *menv);
		
struct sieve_side_effects_list *sieve_side_effects_list_create
	(struct sieve_result *result);
void sieve_side_effects_list_add
(struct sieve_side_effects_list *list, const struct sieve_side_effect *seffect, 
	void *context);

#endif
