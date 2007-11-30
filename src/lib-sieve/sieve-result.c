#include "lib.h"
#include "mempool.h"
#include "strfuncs.h"
#include "str-sanitize.h"

#include "sieve-common.h"
#include "sieve-interpreter.h"
#include "sieve-actions.h"
#include "sieve-result.h"

#include <stdio.h>

struct sieve_result_action {
	struct sieve_result *result;
	const struct sieve_action *action;
	void *context;
	void *tr_context;
	bool success;
	
	struct sieve_side_effects_list *seffects;
	
	struct sieve_result_action *prev, *next; 
};

struct sieve_side_effects_list {
	struct sieve_result *result;

	struct sieve_result_side_effect *first_effect;
	struct sieve_result_side_effect *last_effect;
};

struct sieve_result_side_effect {
	const struct sieve_side_effect *seffect;
	void *context;
	struct sieve_result_side_effect *prev, *next; 
};

struct sieve_result {
	pool_t pool;
	
	struct sieve_action_exec_env action_env;
	struct sieve_result_action *first_action;
	struct sieve_result_action *last_action;
};

struct sieve_result *sieve_result_create(void) 
{
	pool_t pool;
	struct sieve_result *result;
	
	pool = pool_alloconly_create("sieve_result", 4096);	
	result = p_new(pool, struct sieve_result, 1);
	result->pool = pool;
	result->action_env.result = result;
		
	result->first_action = NULL;
	result->last_action = NULL;

	return result;
}

void sieve_result_ref(struct sieve_result *result) 
{
	pool_ref(result->pool);
}

void sieve_result_unref(struct sieve_result **result) 
{
	if ( result != NULL && *result != NULL ) {
		pool_t pool = (*result)->pool;
		pool_unref(&pool);
		if ( pool == NULL )
			*result = NULL;
	}
}

inline pool_t sieve_result_pool(struct sieve_result *result)
{
	return result->pool;
}

/* Logging of result */

void sieve_result_log
	(const struct sieve_action_exec_env *aenv, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	
	/* Kludgy, needs explict support from liblib.a (something like i_vinfo) */
	
	i_info("msgid=%s: %s", aenv->msgdata->id == NULL ? 
		"unspecified" : str_sanitize(aenv->msgdata->id, 80), 
		t_strdup_vprintf(fmt, args)); 
	
	va_end(args);
}

void sieve_result_error
	(const struct sieve_action_exec_env *aenv, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	
	/* Kludgy, needs explict support from liblib.a (something like i_vinfo) */
	
	i_error("msgid=%s: %s", aenv->msgdata->id == NULL ? 
		"unspecified" : str_sanitize(aenv->msgdata->id, 80), 
		t_strdup_vprintf(fmt, args)); 
	
	va_end(args);
}

/* Result composition */

bool sieve_result_add_action
(const struct sieve_runtime_env *renv,
	const struct sieve_action *action, struct sieve_side_effects_list *seffects,
	void *context)		
{
	struct sieve_result *result = renv->result;
	struct sieve_result_action *raction;
		
	/* First, check for duplicates or conflicts */
	raction = result->first_action;
	while ( raction != NULL ) {
		const struct sieve_action *oact = raction->action;
		
		if ( raction->action == action ) {
			/* Possible duplicate */
			if ( action->check_duplicate != NULL ) {
				if ( action->check_duplicate
					(renv, action, raction->context, context) )
					return FALSE;
			} else 
				return FALSE; 
		} else {
			/* Check conflict */
			if ( action->check_conflict != NULL &&
				action->check_conflict(renv, action, oact, context) ) 
				return FALSE;
			
			if ( oact->check_conflict != NULL &&
				oact->check_conflict(renv, oact, action, raction->context) )
				return FALSE;
		}
		raction = raction->next;
	}
		
	/* Create new action object */
	raction = p_new(result->pool, struct sieve_result_action, 1);
	raction->result = result;
	raction->action = action;
	raction->context = context;
	raction->tr_context = NULL;
	raction->success = FALSE;
	raction->seffects = seffects;
	
	/* Add */
	if ( result->first_action == NULL ) {
		result->first_action = raction;
		result->last_action = raction;
		raction->prev = NULL;
		raction->next = NULL;
	} else {
		result->last_action->next = raction;
		raction->prev = result->last_action;
		result->last_action = raction;
		raction->next = NULL;
	}	
	
	return TRUE;
}	

bool sieve_result_print(struct sieve_result *result)
{
	bool implicit_keep = TRUE;
	struct sieve_result_action *rac = result->first_action;
	
	printf("\nPerformed actions:\n");
	while ( rac != NULL ) {		
		bool keep = TRUE;
		const struct sieve_action *act = rac->action;
		struct sieve_result_side_effect *rsef;
		const struct sieve_side_effect *sef;

	
		if ( act->print != NULL ) {
			act->print(act, rac->context, &keep);
		} else {
			printf("* %s\n", act->name); 
		}
	
		/* Print side effects */
		rsef = rac->seffects != NULL ? rac->seffects->first_effect : NULL;
		while ( rsef != NULL ) {
			sef = rsef->seffect;
			if ( sef->print != NULL ) 
				sef->print
					(sef, act, rsef->context, &keep);
			rsef = rsef->next;
		}

		implicit_keep = implicit_keep && keep;		
		rac = rac->next;	
	}
	
	printf("\nImplicit keep: %s\n", implicit_keep ? "yes" : "no");
	
	return TRUE;
}

bool sieve_result_execute
	(struct sieve_result *result, const struct sieve_message_data *msgdata,
		const struct sieve_mail_environment *menv)
{ 
	bool implicit_keep = TRUE;
	bool success = TRUE, commit_ok;
	struct sieve_result_action *rac;
	struct sieve_result_action *last_attempted;

	result->action_env.msgdata = msgdata;
	result->action_env.mailenv = menv;
	
	/* Transaction start */
	
	printf("\nTransaction start:\n");
	
	rac = result->first_action;
	while ( success && rac != NULL ) {
		const struct sieve_action *act = rac->action;
	
		if ( act->start != NULL ) {
			rac->success = act->start(act, &result->action_env, rac->context, 
				&rac->tr_context);
			success = success && rac->success;
		} 
		rac = rac->next;	
	}
	
	/* Transaction execute */
	
	printf("\nTransaction execute:\n");
	
	last_attempted = rac;
	rac = result->first_action;
	while ( success && rac != NULL ) {
		const struct sieve_action *act = rac->action;
		struct sieve_result_side_effect *rsef;
		const struct sieve_side_effect *sef;
		void *context = rac->tr_context == NULL ? 
				rac->context : rac->tr_context;
		
		/* Execute pre-execute event of side effects */
		rsef = rac->seffects != NULL ? rac->seffects->first_effect : NULL;
		while ( rsef != NULL ) {
			sef = rsef->seffect;
			if ( sef->pre_execute != NULL ) 
				sef->pre_execute
					(sef, act, &result->action_env, &rsef->context, context);
			rsef = rsef->next;
		}
	
		/* Execute the action itself */
		if ( act->execute != NULL ) {
			rac->success = act->execute(act, &result->action_env, context);
			success = success && rac->success;
		}
		
		/* Execute pre-execute event of side effects */
		rsef = rac->seffects != NULL ? rac->seffects->first_effect : NULL;
		while ( rsef != NULL ) {
			sef = rsef->seffect;
			if ( sef->post_execute != NULL ) 
				sef->post_execute
					(sef, act, &result->action_env, rsef->context, context);
			rsef = rsef->next;
		}
		 
		rac = rac->next;	
	}
	
	/* Transaction commit/rollback */
	if ( success )
		printf("\nTransaction commit:\n");
	else
		printf("\nTransaction rollback:\n");

	commit_ok = success;
	rac = result->first_action;
	while ( rac != NULL && rac != last_attempted ) {
		const struct sieve_action *act = rac->action;
		struct sieve_result_side_effect *rsef;
		const struct sieve_side_effect *sef;
		void *context = rac->tr_context == NULL ? 
				rac->context : rac->tr_context;
		
		if ( success ) {
			bool keep = TRUE;
		
			if ( act->commit != NULL ) 
				commit_ok = act->commit(act, &result->action_env, context, &keep) && 
					commit_ok;
	
			/* Execute post_commit event of side effects */
			rsef = rac->seffects != NULL ? rac->seffects->first_effect : NULL;
			while ( rsef != NULL ) {
				sef = rsef->seffect;
				if ( sef->post_commit != NULL ) 
					sef->post_commit
						(sef, act, &result->action_env, rsef->context, context, 
							&keep);
				rsef = rsef->next;
			}
			
			implicit_keep = implicit_keep && keep;
		} else {
			if ( act->rollback != NULL ) 
				act->rollback(act, &result->action_env, context, rac->success);
				
			/* Rollback side effects */
			rsef = rac->seffects != NULL ? rac->seffects->first_effect : NULL;
			while ( rsef != NULL ) {
				sef = rsef->seffect;
				if ( sef->rollback != NULL ) 
					sef->rollback
						(sef, act, &result->action_env, rsef->context, context, 
						rac->success);
				rsef = rsef->next;
			}
		}
		
		rac = rac->next;	
	}
	
	printf("\nTransaction result: %s\n", commit_ok ? "success" : "failed");
	
	return commit_ok;
}

/*
 * Side effects list
 */
struct sieve_side_effects_list *sieve_side_effects_list_create
	(struct sieve_result *result)
{
	struct sieve_side_effects_list *list = 
		p_new(result->pool, struct sieve_side_effects_list, 1);
	
	list->result = result;
	list->first_effect = NULL;
	list->last_effect = NULL;
	
	return list;
};

void sieve_side_effects_list_add
(struct sieve_side_effects_list *list, const struct sieve_side_effect *seffect, 
	void *context)		
{
	struct sieve_result_side_effect *reffect;
	
	/* Create new action object */
	reffect = p_new(list->result->pool, struct sieve_result_side_effect, 1);
	reffect->seffect = seffect;
	reffect->context = context;
	
	/* Add */
	if ( list->first_effect == NULL ) {
		list->first_effect = reffect;
		list->last_effect = reffect;
		reffect->prev = NULL;
		reffect->next = NULL;
	} else {
		list->last_effect->next = reffect;
		reffect->prev = list->last_effect;
		list->last_effect = reffect;
		reffect->next = NULL;
	}	
}	



