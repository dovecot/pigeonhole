#include "lib.h"
#include "mempool.h"
#include "ostream.h"
#include "hash.h"
#include "str.h"
#include "strfuncs.h"
#include "str-sanitize.h"

#include "sieve-common.h"
#include "sieve-script.h"
#include "sieve-error.h"
#include "sieve-interpreter.h"
#include "sieve-actions.h"
#include "sieve-result.h"

#include <stdio.h>

/*
 *
 */

struct sieve_result_action {
	struct sieve_result *result;
	const struct sieve_action *action;
	void *context;
	void *tr_context;
	bool success;
	
	const char *location;

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

struct sieve_result_implicit_side_effects {
	const struct sieve_action *action;
	struct sieve_side_effects_list *seffects;
};

struct sieve_result {
	pool_t pool;
	int refcount;

	/* Context data for extensions */
	ARRAY_DEFINE(ext_contexts, void *); 

	struct sieve_error_handler *ehandler;
		
	struct sieve_action_exec_env action_env;
	struct sieve_result_action *first_action;
	struct sieve_result_action *last_action;
	
	struct hash_table *implicit_seffects;
};

struct sieve_result *sieve_result_create
(struct sieve_error_handler *ehandler) 
{
	pool_t pool;
	struct sieve_result *result;
	 
	pool = pool_alloconly_create("sieve_result", 4096);	
	result = p_new(pool, struct sieve_result, 1);
	result->refcount = 1;
	result->pool = pool;
	
	p_array_init(&result->ext_contexts, pool, 4);
	
	result->ehandler = ehandler;
	sieve_error_handler_ref(ehandler);

	result->action_env.result = result;
		
	result->first_action = NULL;
	result->last_action = NULL;

	result->implicit_seffects = NULL;
	return result;
}

void sieve_result_ref(struct sieve_result *result) 
{
	result->refcount++;
}

void sieve_result_unref(struct sieve_result **result) 
{
	i_assert((*result)->refcount > 0);

	if (--(*result)->refcount != 0)
		return;

	if ( (*result)->implicit_seffects != NULL )
        hash_destroy(&(*result)->implicit_seffects);

	sieve_error_handler_unref(&(*result)->ehandler);

	pool_unref(&(*result)->pool);

 	*result = NULL;
}

pool_t sieve_result_pool(struct sieve_result *result)
{
	return result->pool;
}

void sieve_result_extension_set_context
(struct sieve_result *result, const struct sieve_extension *ext, void *context)
{
	array_idx_set(&result->ext_contexts, (unsigned int) *ext->id, &context);	
}

const void *sieve_result_extension_get_context
(struct sieve_result *result, const struct sieve_extension *ext) 
{
	int ext_id = *ext->id;
	void * const *ctx;

	if  ( ext_id < 0 || ext_id >= (int) array_count(&result->ext_contexts) )
		return NULL;
	
	ctx = array_idx(&result->ext_contexts, (unsigned int) ext_id);		

	return *ctx;
}

/* Logging of result */

static const char *_get_location(const struct sieve_action_exec_env *aenv)
{
	return t_strdup_printf("msgid=%s", aenv->msgdata->id == NULL ? 
		"unspecified" : str_sanitize(aenv->msgdata->id, 80));
}

void sieve_result_error
	(const struct sieve_action_exec_env *aenv, const char *fmt, ...)
{
	va_list args;
	
	va_start(args, fmt);	
	sieve_verror(aenv->result->ehandler, _get_location(aenv), fmt, args); 
	va_end(args);
}

void sieve_result_warning
	(const struct sieve_action_exec_env *aenv, const char *fmt, ...)
{
	va_list args;
	
	va_start(args, fmt);	
	sieve_vwarning(aenv->result->ehandler, _get_location(aenv), fmt, args); 
	va_end(args);
}

void sieve_result_log
	(const struct sieve_action_exec_env *aenv, const char *fmt, ...)
{
	va_list args;
	
	va_start(args, fmt);	
	sieve_vinfo(aenv->result->ehandler, _get_location(aenv), fmt, args); 
	va_end(args);
}

/* Result composition */

void sieve_result_add_implicit_side_effect
(struct sieve_result *result, const struct sieve_action *to_action, 
	const struct sieve_side_effect *seffect, void *context)
{
	struct sieve_result_implicit_side_effects *implseff = NULL;
	
	if ( result->implicit_seffects == NULL ) {
		result->implicit_seffects = hash_create
			(default_pool, result->pool, 0, NULL, NULL);
	} else {
		implseff = (struct sieve_result_implicit_side_effects *) 
			hash_lookup(result->implicit_seffects, to_action);
	}

	if ( implseff == NULL ) {
		implseff = p_new
			(result->pool, struct sieve_result_implicit_side_effects, 1);
		implseff->action = to_action;
		implseff->seffects = sieve_side_effects_list_create(result);
		
		hash_insert(result->implicit_seffects, (void *) to_action, 
			(void *) implseff);
	}	
	
	sieve_side_effects_list_add(implseff->seffects, seffect, context);
}

int sieve_result_add_action
(const struct sieve_runtime_env *renv,
	const struct sieve_action *action, struct sieve_side_effects_list *seffects,
	unsigned int source_line, void *context)		
{
	int ret = 0;
	struct sieve_result *result = renv->result;
	struct sieve_result_action *raction;
	const char *location = sieve_error_script_location
		(renv->script, source_line);
		
	/* First, check for duplicates or conflicts */
	raction = result->first_action;
	while ( raction != NULL ) {
		const struct sieve_action *oact = raction->action;
		
		if ( raction->action == action ) {
			/* Possible duplicate */
			if ( action->check_duplicate != NULL ) {
				if ( (ret=action->check_duplicate
					(renv, action, context, raction->context,
						location, raction->location)) != 0 )
					return ret;
			} else 
				return 1; 
		} else {
			/* Check conflict */
			if ( action->check_conflict != NULL &&
				(ret=action->check_conflict(renv, action, oact, context,
					location, raction->location)) != 0 ) 
				return ret;
			
			if ( oact->check_conflict != NULL &&
				(ret=oact->check_conflict(renv, oact, action, raction->context,
					raction->location, location)) != 0 )
				return ret;
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
	raction->location = p_strdup(result->pool, location);

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
	
	/* Apply any implicit side effects */
	if ( result->implicit_seffects != NULL ) {
		struct sieve_result_implicit_side_effects *implseff;
		
		/* Check for implicit side effects to this particular action */
		implseff = (struct sieve_result_implicit_side_effects *) 
				hash_lookup(result->implicit_seffects, action);
		
		if ( implseff != NULL ) {
			struct sieve_result_side_effect *iseff;
			
			/* Iterate through all implicit side effects and add those that are 
			 * missing.
			 */
			iseff = implseff->seffects->first_effect;
			while ( iseff != NULL ) {
				struct sieve_result_side_effect *seff;
				bool exists = FALSE;
				
				/* Scan for presence */
				if ( seffects != NULL ) {
					seff = seffects->first_effect;
					while ( seff != NULL ) {
						if ( seff->seffect == iseff->seffect ) {
							exists = TRUE;
							break;
						}
					
						seff = seff->next;
					}
				} else {
					raction->seffects = seffects = sieve_side_effects_list_create(result);
				}
				
				/* If not present, add it */
				if ( !exists ) {
					sieve_side_effects_list_add(seffects, iseff->seffect, iseff->context);
				}
				
				iseff = iseff->next;
			}
		}
	}
	
	return 0;
}

/*
 * Result printing
 */

void sieve_result_printf
(const struct sieve_result_print_env *penv, const char *fmt, ...)
{	
	string_t *outbuf = t_str_new(128);
	va_list args;
	
	va_start(args, fmt);	
	str_vprintfa(outbuf, fmt, args);
	va_end(args);
	
	o_stream_send(penv->stream, str_data(outbuf), str_len(outbuf));
}

void sieve_result_action_printf
(const struct sieve_result_print_env *penv, const char *fmt, ...)
{	
	string_t *outbuf = t_str_new(128);
	va_list args;
	
	va_start(args, fmt);	
	str_append(outbuf, " * ");
	str_vprintfa(outbuf, fmt, args);
	str_append_c(outbuf, '\n');
	va_end(args);
	
	o_stream_send(penv->stream, str_data(outbuf), str_len(outbuf));
}

void sieve_result_seffect_printf
(const struct sieve_result_print_env *penv, const char *fmt, ...)
{	
	string_t *outbuf = t_str_new(128);
	va_list args;
	
	va_start(args, fmt);	
	str_append(outbuf, "        + ");
	str_vprintfa(outbuf, fmt, args);
	str_append_c(outbuf, '\n');
	va_end(args);
	
	o_stream_send(penv->stream, str_data(outbuf), str_len(outbuf));
}

bool sieve_result_print
(struct sieve_result *result, struct ostream *stream)
{
	struct sieve_result_print_env penv;
	bool implicit_keep = TRUE;
	struct sieve_result_action *rac;
	
	penv.result = result;
	penv.stream = stream;
	
	sieve_result_printf(&penv, "\nPerformed actions:\n\n");
	rac = result->first_action;
	while ( rac != NULL ) {		
		bool keep = TRUE;
		const struct sieve_action *act = rac->action;
		struct sieve_result_side_effect *rsef;
		const struct sieve_side_effect *sef;

		if ( act->print != NULL ) {
			act->print(act, &penv, rac->context, &keep);
		} else {
			sieve_result_action_printf(&penv, act->name); 
		}
	
		/* Print side effects */
		rsef = rac->seffects != NULL ? rac->seffects->first_effect : NULL;
		while ( rsef != NULL ) {
			sef = rsef->seffect;
			if ( sef->print != NULL ) 
				sef->print
					(sef, act, &penv, rsef->context, &keep);
			rsef = rsef->next;
		}
			
		implicit_keep = implicit_keep && keep;		
		rac = rac->next;	
	}
	
	sieve_result_printf
		(&penv, "\nImplicit keep: %s\n", implicit_keep ? "yes" : "no");
	
	return TRUE;
}

bool sieve_result_implicit_keep
	(struct sieve_result *result, bool rollback)
{	
	bool success = TRUE;
	bool dummy = TRUE;
	struct act_store_context ctx;
	struct sieve_result_side_effect *rsef, *rsef_first = NULL;
	void *tr_context;
	
	ctx.folder = result->action_env.scriptenv->inbox;
	
	/* Also apply any implicit side effects if applicable */
	if ( !rollback && result->implicit_seffects != NULL ) {
		struct sieve_result_implicit_side_effects *implseff;
		
		/* Check for implicit side effects to store action */
		implseff = (struct sieve_result_implicit_side_effects *) 
				hash_lookup(result->implicit_seffects, &act_store);
		
		if ( implseff != NULL && implseff->seffects != NULL ) 
			rsef_first = implseff->seffects->first_effect;
		
	}
	
	success = act_store.start
		(&act_store, &result->action_env, (void *) &ctx, &tr_context);

	rsef = rsef_first;
	while ( rsef != NULL ) {
		const struct sieve_side_effect *sef = rsef->seffect;
		if ( sef->pre_execute != NULL ) 
			success = success & sef->pre_execute
				(sef, &act_store, &result->action_env, &rsef->context, tr_context);
		rsef = rsef->next;
	}

	success = success && act_store.execute
			(&act_store, &result->action_env, tr_context);
			
	rsef = rsef_first;
	while ( rsef != NULL ) {
		const struct sieve_side_effect *sef = rsef->seffect;
		if ( sef->post_execute != NULL ) 
			success = success && sef->post_execute
				(sef, &act_store, &result->action_env, rsef->context, tr_context);
		rsef = rsef->next;
	}
	
	if ( success ) {	
		success = act_store.commit
			(&act_store, &result->action_env, tr_context, &dummy);

		rsef = rsef_first;
		while ( rsef != NULL ) {
			const struct sieve_side_effect *sef = rsef->seffect;
			bool keep = TRUE;
			
			if ( sef->post_commit != NULL ) 
				sef->post_commit
					(sef, &act_store, &result->action_env, rsef->context, tr_context, 
						&keep);
			rsef = rsef->next;
		}
			
		return success; 
	}
		
	act_store.rollback(&act_store, &result->action_env, tr_context, success);

	return FALSE;
}

int sieve_result_execute
	(struct sieve_result *result, const struct sieve_message_data *msgdata,
		const struct sieve_script_env *senv)
{ 
	bool implicit_keep = TRUE;
	bool success = TRUE, commit_ok;
	struct sieve_result_action *rac;
	struct sieve_result_action *last_attempted;

	result->action_env.msgdata = msgdata;
	result->action_env.scriptenv = senv;
	
	/* 
     * Transaction start 
     */
	
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
	
	/* 
     * Transaction execute 
     */
	
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
				success = success & sef->pre_execute
					(sef, act, &result->action_env, &rsef->context, context);
			rsef = rsef->next;
		}
	
		/* Execute the action itself */
		if ( act->execute != NULL ) {
			rac->success = act->execute(act, &result->action_env, context);
			success = success && rac->success;
		}
		
		/* Execute post-execute event of side effects */
		rsef = rac->seffects != NULL ? rac->seffects->first_effect : NULL;
		while ( rsef != NULL ) {
			sef = rsef->seffect;
			if ( sef->post_execute != NULL ) 
				success = success && sef->post_execute
					(sef, act, &result->action_env, rsef->context, context);
			rsef = rsef->next;
		}
		 
		rac = rac->next;	
	}
	
	/* 
     * Transaction commit/rollback 
     */

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
	
	/* Return value indicates whether the caller should attempt an implicit keep 
	 * of its own. So, if the above transaction fails, but the implicit keep below
	 * succeeds, the return value is still true. An error is/should be logged 
	 * though.
	 */
	
	/* Execute implicit keep if the transaction failed or when the implicit keep
	 * was not canceled during transaction. 
	 */
	if ( !commit_ok || implicit_keep ) {		
		if ( !sieve_result_implicit_keep(result, !commit_ok) ) 
			return SIEVE_EXEC_KEEP_FAILED;
			
		return ( commit_ok ? 
			SIEVE_EXEC_OK            /* Success */ :
			SIEVE_EXEC_FAILURE       /* Implicit keep executed */ );
	}
	
	/* Unconditional success */
	return SIEVE_EXEC_OK;
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
	
	/* Create new side effect object */
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



