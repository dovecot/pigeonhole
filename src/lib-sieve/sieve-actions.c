#include "sieve-interpreter.h"
#include "sieve-result.h"
#include "sieve-actions.h"

/*
 * Actions common to multiple core commands 
 */
 
/* Store action */

static bool act_store_check_duplicate
	(const struct sieve_runtime_env *renv ATTR_UNUSED,
		const struct sieve_action *action1 ATTR_UNUSED, 
		void *context1, void *context2);
static void act_store_print
	(const struct sieve_action *action ATTR_UNUSED, void *context);

static bool act_store_start
	(const struct sieve_action *action,
		const struct sieve_action_exec_env *aenv, void *context, void **tr_context);
static bool act_store_execute
	(const struct sieve_action *action ATTR_UNUSED, 
		const struct sieve_action_exec_env *aenv, void *tr_context);
static bool act_store_commit
	(const struct sieve_action *action ATTR_UNUSED, 
		const struct sieve_action_exec_env *aenv, void *tr_context);
static void act_store_rollback
	(const struct sieve_action *action ATTR_UNUSED, 
		const struct sieve_action_exec_env *aenv, void *tr_context);
		
const struct sieve_action act_store = {
	"store",
	act_store_check_duplicate, 
	NULL, 
	act_store_print,
	act_store_start,
	act_store_execute,
	act_store_commit,
	act_store_rollback,
};

bool sieve_act_store_add_to_result
(const struct sieve_runtime_env *renv, const char *folder)
{
	pool_t pool;
	struct act_store_context *act;
	
	/* Add redirect action to the result */
	pool = sieve_result_pool(renv->result);
	act = p_new(pool, struct act_store_context, 1);
	act->folder = p_strdup(pool, folder);

	return sieve_result_add_action(renv, &act_store, (void *) act);
}

/* Store action implementation */

static bool act_store_check_duplicate
(const struct sieve_runtime_env *renv ATTR_UNUSED,
	const struct sieve_action *action1 ATTR_UNUSED, 
	void *context1, void *context2)
{
	struct act_store_context *ctx1 = (struct act_store_context *) context1;
	struct act_store_context *ctx2 = (struct act_store_context *) context2;
	
	if ( strcmp(ctx1->folder, ctx2->folder) == 0 ) 
		return TRUE;
		
	return FALSE;
}

static void act_store_print
(const struct sieve_action *action ATTR_UNUSED, void *context)	
{
	struct act_store_context *ctx = (struct act_store_context *) context;
	
	printf("* store message in folder: %s\n", ctx->folder);
}

/* Store transaction */

static bool act_store_start
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv, void *context, void **tr_context)
{  
	return TRUE;
}

static bool act_store_execute
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv, void *tr_context)
{  
	return TRUE;
}

static bool act_store_commit
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv, void *tr_context)
{  
	return TRUE;
}

static void act_store_rollback
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv, void *tr_context)
{  
}




