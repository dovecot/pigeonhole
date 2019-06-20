/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-execute.h"

void sieve_execute_init(struct sieve_execute_env *eenv,
			struct sieve_instance *svinst, pool_t pool,
			const struct sieve_message_data *msgdata,
			const struct sieve_script_env *senv,
			enum sieve_execute_flags flags)
{
	i_zero(eenv);
	eenv->svinst = svinst;
	eenv->pool = pool;
	eenv->flags = flags;
	eenv->msgdata = msgdata;
	eenv->scriptenv = senv;

	pool_ref(pool);

	eenv->exec_status = senv->exec_status;
	if (eenv->exec_status == NULL)
		eenv->exec_status = p_new(pool, struct sieve_exec_status, 1);
	else
		i_zero(eenv->exec_status);
}

void sieve_execute_deinit(struct sieve_execute_env *eenv)
{
	pool_unref(&eenv->pool);
}

