#ifndef SIEVE_EXECUTE_H
#define SIEVE_EXECUTE_H

#include "sieve-common.h"

struct sieve_execute_state;

struct sieve_execute_env {
	struct sieve_instance *svinst;
	pool_t pool;

	enum sieve_execute_flags flags;
	struct event *event;

	const struct sieve_message_data *msgdata;
	const struct sieve_script_env *scriptenv;

	struct sieve_execute_state *state;
	struct sieve_exec_status *exec_status;
};

void sieve_execute_init(struct sieve_execute_env *eenv,
			struct sieve_instance *svinst, pool_t pool,
			const struct sieve_message_data *msgdata,
			const struct sieve_script_env *senv,
			enum sieve_execute_flags flags);
void sieve_execute_finish(struct sieve_execute_env *eenv, int status);
void sieve_execute_deinit(struct sieve_execute_env *eenv);

/*
 * Checking for duplicates
 */

bool sieve_execute_duplicate_check_available(
	const struct sieve_execute_env *eenv);
int sieve_execute_duplicate_check(const struct sieve_execute_env *eenv,
				  const void *id, size_t id_size,
				  bool *duplicate_r);
void sieve_execute_duplicate_mark(const struct sieve_execute_env *eenv,
				  const void *id, size_t id_size, time_t time);

#endif
