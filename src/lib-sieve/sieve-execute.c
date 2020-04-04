/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-execute.h"

struct event_category event_category_sieve_execute = {
	.parent = &event_category_sieve,
	.name = "sieve-execute",
};

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
	eenv->event = event_create(svinst->event);
	event_add_category(eenv->event, &event_category_sieve_execute);
	event_add_str(eenv->event, "message_id", msgdata->id);
	if ((flags & SIEVE_EXECUTE_FLAG_NO_ENVELOPE) == 0) {
		/* Make sure important envelope fields are available */
		event_add_str(eenv->event, "mail_from",
			smtp_address_encode(msgdata->envelope.mail_from));
		event_add_str(eenv->event, "rcpt_to",
			smtp_address_encode(msgdata->envelope.rcpt_to));
	}

	eenv->exec_status = senv->exec_status;
	if (eenv->exec_status == NULL)
		eenv->exec_status = p_new(pool, struct sieve_exec_status, 1);
	else
		i_zero(eenv->exec_status);
}

void sieve_execute_deinit(struct sieve_execute_env *eenv)
{
	event_unref(&eenv->event);
	pool_unref(&eenv->pool);
}

