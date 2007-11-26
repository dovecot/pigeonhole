#ifndef __SIEVE_ACTIONS_H
#define __SIEVE_ACTIONS_H

#include "sieve-common.h"

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

/* Actions common to multiple commands */

const struct sieve_action act_store;

struct act_store_context {
	const char *folder;
};

bool sieve_act_store_add_to_result
	(const struct sieve_runtime_env *renv, const char *folder);

		
#endif /* __SIEVE_ACTIONS_H */
