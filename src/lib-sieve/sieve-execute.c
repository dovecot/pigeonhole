/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-execute.h"

struct sieve_execute_state {
	void *dup_trans;
};

struct event_category event_category_sieve_execute = {
	.parent = &event_category_sieve,
	.name = "sieve-execute",
};

static struct sieve_execute_state *
sieve_execute_state_create(struct sieve_execute_env *eenv)
{
	return p_new(eenv->pool, struct sieve_execute_state, 1);
}

static void
sieve_execute_state_free(struct sieve_execute_state **_estate,
			 struct sieve_execute_env *eenv)
{
	struct sieve_execute_state *estate = *_estate;
	const struct sieve_script_env *senv = eenv->scriptenv;

	*_estate = NULL;

	if (senv->duplicate_transaction_rollback != NULL)
		senv->duplicate_transaction_rollback(&estate->dup_trans);
}

void sieve_execute_init(struct sieve_execute_env *eenv,
			struct sieve_instance *svinst, pool_t pool,
			const struct sieve_message_data *msgdata,
			const struct sieve_script_env *senv,
			enum sieve_execute_flags flags)
{
	i_assert(svinst->username != NULL);

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

	eenv->state = sieve_execute_state_create(eenv);

	eenv->exec_status = senv->exec_status;
	if (eenv->exec_status == NULL)
		eenv->exec_status = p_new(pool, struct sieve_exec_status, 1);
	else
		i_zero(eenv->exec_status);
}

void sieve_execute_finish(struct sieve_execute_env *eenv, int status)
{
	const struct sieve_script_env *senv = eenv->scriptenv;

	if (status == SIEVE_EXEC_OK) {
		if (senv->duplicate_transaction_commit != NULL) {
			senv->duplicate_transaction_commit(
				&eenv->state->dup_trans);
		}
	} else {
		if (senv->duplicate_transaction_rollback != NULL) {
			senv->duplicate_transaction_rollback(
				&eenv->state->dup_trans);
		}
	}
}

void sieve_execute_deinit(struct sieve_execute_env *eenv)
{
	sieve_execute_state_free(&eenv->state, eenv);
	event_unref(&eenv->event);
	pool_unref(&eenv->pool);
}

/*
 * Checking for duplicates
 */

static void *
sieve_execute_get_dup_transaction(const struct sieve_execute_env *eenv)
{
	const struct sieve_script_env *senv = eenv->scriptenv;

	if (senv->duplicate_transaction_begin == NULL)
		return NULL;
	if (eenv->state->dup_trans == NULL) {
		eenv->state->dup_trans =
			senv->duplicate_transaction_begin(senv);
	}
	return eenv->state->dup_trans;
}

bool sieve_execute_duplicate_check_available(
	const struct sieve_execute_env *eenv)
{
	const struct sieve_script_env *senv = eenv->scriptenv;

	return (senv->duplicate_transaction_begin != NULL);
}

int sieve_execute_duplicate_check(const struct sieve_execute_env *eenv,
				  const void *id, size_t id_size,
				  bool *duplicate_r)
{
	const struct sieve_script_env *senv = eenv->scriptenv;
	void *dup_trans = sieve_execute_get_dup_transaction(eenv);
	int ret;

	*duplicate_r = FALSE;

	if (senv->duplicate_check == NULL)
		return SIEVE_EXEC_OK;

	e_debug(eenv->svinst->event, "Check duplicate ID");

	ret = senv->duplicate_check(dup_trans, senv, id, id_size);
	switch (ret) {
	case SIEVE_DUPLICATE_CHECK_RESULT_EXISTS:
		*duplicate_r = TRUE;
		break;
	case SIEVE_DUPLICATE_CHECK_RESULT_NOT_FOUND:
		break;
	case SIEVE_DUPLICATE_CHECK_RESULT_FAILURE:
		return SIEVE_EXEC_FAILURE;
	case SIEVE_DUPLICATE_CHECK_RESULT_TEMP_FAILURE:
		return SIEVE_EXEC_TEMP_FAILURE;
	}
	return SIEVE_EXEC_OK;
}

void sieve_execute_duplicate_mark(const struct sieve_execute_env *eenv,
				  const void *id, size_t id_size, time_t time)
{
	const struct sieve_script_env *senv = eenv->scriptenv;
	void *dup_trans = sieve_execute_get_dup_transaction(eenv);

	if (senv->duplicate_mark == NULL)
		return;

	e_debug(eenv->svinst->event, "Mark ID as duplicate");

	senv->duplicate_mark(dup_trans, senv, id, id_size, time);
}
