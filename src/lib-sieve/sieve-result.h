#ifndef __SIEVE_RESULT_H
#define __SIEVE_RESULT_H

#include "sieve-common.h"

struct sieve_result;

struct sieve_result *sieve_result_create(void);
void sieve_result_ref(struct sieve_result *result); 
void sieve_result_unref(struct sieve_result **result); 
inline pool_t sieve_result_pool(struct sieve_result *result);

bool sieve_result_add_action
(const struct sieve_runtime_env *renv, const struct sieve_action *action, 
	void *context);	

bool sieve_result_print(struct sieve_result *result);

bool sieve_result_execute
	(struct sieve_result *result, const struct sieve_message_data *msgdata,
		const struct sieve_mail_environment *menv);

#endif
