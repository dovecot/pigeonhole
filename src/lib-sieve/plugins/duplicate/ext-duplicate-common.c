/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "md5.h"
#include "ioloop.h"
#include "str.h"
#include "str-sanitize.h"
#include "array.h"

#include "sieve-common.h"
#include "sieve-settings.old.h"
#include "sieve-error.h"
#include "sieve-extensions.h"
#include "sieve-message.h"
#include "sieve-code.h"
#include "sieve-runtime.h"
#include "sieve-interpreter.h"
#include "sieve-actions.h"
#include "sieve-result.h"

#include "ext-duplicate-common.h"

/*
 * Extension configuration
 */

#define EXT_DUPLICATE_DEFAULT_PERIOD (12*60*60)
#define EXT_DUPLICATE_DEFAULT_MAX_PERIOD (2*24*60*60)

int ext_duplicate_load(const struct sieve_extension *ext, void **context_r)
{
	struct sieve_instance *svinst = ext->svinst;
	struct ext_duplicate_context *extctx;
	sieve_number_t default_period, max_period;

	if (!sieve_setting_get_duration_value(
		svinst, "sieve_duplicate_default_period", &default_period))
		default_period = EXT_DUPLICATE_DEFAULT_PERIOD;
	if (!sieve_setting_get_duration_value(
		svinst, "sieve_duplicate_max_period", &max_period)) {
		max_period = EXT_DUPLICATE_DEFAULT_MAX_PERIOD;
	}

	extctx = i_new(struct ext_duplicate_context, 1);
	extctx->default_period = default_period;
	extctx->max_period = max_period;

	*context_r = extctx;
	return 0;
}

void ext_duplicate_unload(const struct sieve_extension *ext)
{
	struct ext_duplicate_context *extctx = ext->context;

	i_free(extctx);
}

/*
 * Duplicate_mark action
 */

struct act_duplicate_mark_data {
	const char *handle;
	unsigned int period;
	unsigned char hash[MD5_RESULTLEN];
	bool last:1;
};

static void
act_duplicate_mark_print(const struct sieve_action *action,
			 const struct sieve_result_print_env *rpenv,
			 bool *keep);
static void
act_duplicate_mark_finish(const struct sieve_action_exec_env *aenv,
			  void *tr_context, int status);

static const struct sieve_action_def act_duplicate_mark = {
	.name = "duplicate_mark",
	.print = act_duplicate_mark_print,
	.finish = act_duplicate_mark_finish,
};

static void
act_duplicate_mark_print(const struct sieve_action *action,
			 const struct sieve_result_print_env *rpenv,
			 bool *keep ATTR_UNUSED)
{
	struct act_duplicate_mark_data *data =
		(struct act_duplicate_mark_data *)action->context;
	const char *last = (data->last ? " last" : "");

	if (data->handle != NULL) {
		sieve_result_action_printf(
			rpenv, "track%s duplicate with handle: %s",
			last, str_sanitize(data->handle, 128));
	} else {
		sieve_result_action_printf(rpenv, "track%s duplicate", last);
	}
}

static void
act_duplicate_mark_finish(const struct sieve_action_exec_env *aenv,
			  void *tr_context ATTR_UNUSED, int status)
{
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct act_duplicate_mark_data *data =
		(struct act_duplicate_mark_data *)aenv->action->context;

	if (status != SIEVE_EXEC_OK) {
		e_debug(aenv->event, "Not marking duplicate (status=%s)",
			sieve_execution_exitcode_to_str(status));
		return;
	}

	e_debug(aenv->event, "Marking duplicate");

	/* Message was handled successfully, so track duplicate for this
	 * message.
	 */
	eenv->exec_status->significant_action_executed = TRUE;
	sieve_action_duplicate_mark(aenv, data->hash, sizeof(data->hash),
				    ioloop_time + data->period);
}

/*
 * Duplicate checking
 */

struct ext_duplicate_handle {
	const char *handle;
	bool last:1;
	bool duplicate:1;
};

struct ext_duplicate_hash {
	unsigned char hash[MD5_RESULTLEN];
	ARRAY(struct ext_duplicate_handle) handles;
};

struct ext_duplicate_runtime_context {
	ARRAY(struct ext_duplicate_hash) hashes;
};

static void
ext_duplicate_hash(string_t *handle, const char *value, size_t value_len,
		   bool last, unsigned char hash_r[])
{
	static const char *id = "sieve duplicate";
	struct md5_context md5ctx;

	md5_init(&md5ctx);
	md5_update(&md5ctx, id, strlen(id));
	if (last)
		md5_update(&md5ctx, "0", 1);
	else
		md5_update(&md5ctx, "+", 1);
	if (handle != NULL) {
		md5_update(&md5ctx, "h-", 2);
		md5_update(&md5ctx, str_c(handle), str_len(handle));
	} else {
		md5_update(&md5ctx, "default", 7);
	}
	md5_update(&md5ctx, value, value_len);
	md5_final(&md5ctx, hash_r);
}

int ext_duplicate_check(const struct sieve_runtime_env *renv, string_t *handle,
			const char *value, size_t value_len,
			sieve_number_t period, bool last, bool *duplicate_r)
{
	const struct sieve_execute_env *eenv = renv->exec_env;
	const struct sieve_extension *this_ext = renv->oprtn->ext;
	struct ext_duplicate_runtime_context *rctx;
	bool duplicate = FALSE;
	pool_t msg_pool = NULL, result_pool = NULL;
	unsigned char hash[MD5_RESULTLEN];
	struct ext_duplicate_hash *hash_record = NULL;
	struct ext_duplicate_handle *handle_record = NULL;
	struct act_duplicate_mark_data *act;
	int ret;

	*duplicate_r = FALSE;

	if (!sieve_execute_duplicate_check_available(eenv)) {
		sieve_runtime_warning(
			renv, NULL, "duplicate test: "
			"duplicate checking not available in this context");
		return SIEVE_EXEC_OK;
	}

	if (value == NULL)
		return SIEVE_EXEC_OK;

	/* Create hash */
	ext_duplicate_hash(handle, value, value_len, last, hash);

	/* Get context; find out whether duplicate was checked earlier */
	rctx = sieve_message_context_extension_get(renv->msgctx, this_ext);

	if (rctx == NULL) {
		/* Create context */
		msg_pool = sieve_message_context_pool(renv->msgctx);
		rctx = p_new(msg_pool, struct ext_duplicate_runtime_context, 1);
		sieve_message_context_extension_set(renv->msgctx, this_ext,
						    rctx);
	} else if (array_is_created(&rctx->hashes)) {
		struct ext_duplicate_hash *record;

		array_foreach_modifiable(&rctx->hashes, record) {
			if (memcmp(record->hash, hash, MD5_RESULTLEN) == 0) {
				hash_record = record;
				break;
			}
		}
	}
	if (hash_record != NULL) {
		const struct ext_duplicate_handle *rhandle;
		array_foreach(&hash_record->handles, rhandle) {
			const char *handle_str =
				(handle == NULL ? NULL : str_c(handle));
			if (null_strcmp(rhandle->handle, handle_str) == 0 &&
			    rhandle->last == last)
				return (rhandle->duplicate ?
				        SIEVE_DUPLICATE_CHECK_RESULT_EXISTS :
					SIEVE_DUPLICATE_CHECK_RESULT_NOT_FOUND);
		}
	}

	result_pool = sieve_result_pool(renv->result);
	act = p_new(result_pool, struct act_duplicate_mark_data, 1);
	if (handle != NULL)
		act->handle = p_strdup(result_pool, str_c(handle));
	act->period = period;
	memcpy(act->hash, hash, MD5_RESULTLEN);
	act->last = last;

	/* Check duplicate */
	ret = sieve_execute_duplicate_check(eenv, hash, sizeof(hash),
					    &duplicate);
	if (ret >= SIEVE_EXEC_OK && !duplicate && last) {
		unsigned char no_last_hash[MD5_RESULTLEN];

		/* Check for entry without :last */
		ext_duplicate_hash(handle, value, value_len,
				   FALSE, no_last_hash);
		ret = sieve_execute_duplicate_check(
			eenv, no_last_hash, sizeof(no_last_hash),
			&duplicate);
	}
	if (ret < SIEVE_EXEC_OK) {
		sieve_runtime_critical(
			renv, NULL, "failed to check for duplicate",
			"failed to check for duplicate%s",
			(ret == SIEVE_EXEC_TEMP_FAILURE ?
			 " (temporary failure)" : ""));
		return ret;
	}

	/* We may only mark the message as duplicate when Sieve script executes
	   successfully; therefore defer this operation until successful result
	   execution.
	 */
	if (!duplicate || last) {
		if (sieve_result_add_action(renv, NULL, NULL,
					    &act_duplicate_mark,
					    NULL, act, 0, FALSE) < 0)
			return SIEVE_EXEC_FAILURE;
	}

	/* Cache result */
	if (msg_pool == NULL)
		msg_pool = sieve_message_context_pool(renv->msgctx);
	if (hash_record == NULL) {
		if (!array_is_created(&rctx->hashes))
			p_array_init(&rctx->hashes, msg_pool, 64);
		hash_record = array_append_space(&rctx->hashes);
		memcpy(hash_record->hash, hash, MD5_RESULTLEN);
		p_array_init(&hash_record->handles, msg_pool, 64);
	}

	handle_record = array_append_space(&hash_record->handles);
	if (handle != NULL)
		handle_record->handle = p_strdup(msg_pool, str_c(handle));
	handle_record->last = last;
	handle_record->duplicate = duplicate;

	*duplicate_r = duplicate;

	return SIEVE_EXEC_OK;
}
