#ifndef SIEVE_EXECUTE_H
#define SIEVE_EXECUTE_H

#include "sieve-common.h"

struct sieve_execute_env {
	struct sieve_instance *svinst;
	enum sieve_execute_flags flags;

	const struct sieve_message_data *msgdata;
	const struct sieve_script_env *scriptenv;

	struct sieve_exec_status *exec_status;
};

#endif
