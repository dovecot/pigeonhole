#ifndef __SIEVE_RESULT_H
#define __SIEVE_RESULT_H

#include "sieve-common.h"

struct sieve_result;

struct sieve_action_exec_env {
	const struct sieve_message_data *msgdata;
	const struct sieve_mail_environment *mailenv;
};

struct sieve_action {
	const char *name;

	bool (*check_duplicate)	
		(const struct sieve_runtime_env *renv,
			const struct sieve_action *action, void *context1, void *context2);	
	bool (*check_conflict)
		(const struct sieve_runtime_env *renv,
			const struct sieve_action *action1, const struct sieve_action *action2,
			void *context1);

	void (*print)
		(const struct sieve_action *action, void *context);	
	int (*execute)
		(const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *context);	
};

struct sieve_result *sieve_result_create(void);
void sieve_result_ref(struct sieve_result *result); 
void sieve_result_unref(struct sieve_result **result); 
inline pool_t sieve_result_pool(struct sieve_result *result);

bool sieve_result_add_action
(struct sieve_result *result, const struct sieve_runtime_env *renv,
	const struct sieve_action *action, void *context);	

bool sieve_result_print(struct sieve_result *result);

bool sieve_result_execute
	(struct sieve_result *result, const struct sieve_message_data *msgdata,
		const struct sieve_mail_environment *menv);

#endif
