#ifndef SIEVE_EXECUTE_H
#define SIEVE_EXECUTE_H

#include "sieve-common.h"

struct sieve_execute_env {
	struct sieve_instance *svinst;
	pool_t pool;

	enum sieve_execute_flags flags;
	struct event *event;

	const struct sieve_message_data *msgdata;
	const struct sieve_script_env *scriptenv;

	struct sieve_exec_status *exec_status;
};

void sieve_execute_init(struct sieve_execute_env *eenv,
			struct sieve_instance *svinst, pool_t pool,
			const struct sieve_message_data *msgdata,
			const struct sieve_script_env *senv,
			enum sieve_execute_flags flags);
void sieve_execute_deinit(struct sieve_execute_env *eenv);

#endif
