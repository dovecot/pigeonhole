/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */
 
#include "lib.h"
#include "mempool.h"
#include "ostream.h"
#include "hash.h"
#include "str.h"
#include "strfuncs.h"
#include "str-sanitize.h"

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-script.h"
#include "sieve-error.h"
#include "sieve-interpreter.h"
#include "sieve-actions.h"
#include "sieve-result.h"

#include <stdio.h>

/*
 * Types
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

struct sieve_result_action_context {
	const struct sieve_action *action;
	struct sieve_side_effects_list *seffects;
};

/*
 * Result object
 */

struct sieve_result {
	pool_t pool;
	int refcount;

	/* Context data for extensions */
	ARRAY_DEFINE(ext_contexts, void *); 

	struct sieve_error_handler *ehandler;
		
	struct sieve_action_exec_env action_env;

	unsigned int action_count;
	struct sieve_result_action *first_action;
	struct sieve_result_action *last_action;
	
	struct hash_table *action_contexts;
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
		
	result->action_count = 0;
	result->first_action = NULL;
	result->last_action = NULL;

	result->action_contexts = NULL;
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

	if ( (*result)->action_contexts != NULL )
        hash_table_destroy(&(*result)->action_contexts);

	sieve_error_handler_unref(&(*result)->ehandler);

	pool_unref(&(*result)->pool);

 	*result = NULL;
}

pool_t sieve_result_pool(struct sieve_result *result)
{
	return result->pool;
}

/*
 * Extension support
 */

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

/* 
 * Error handling 
 */

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

/* 
 * Result composition 
 */

void sieve_result_add_implicit_side_effect
(struct sieve_result *result, const struct sieve_action *to_action, 
	const struct sieve_side_effect *seffect, void *context)
{
	struct sieve_result_action_context *actctx = NULL;
	
	if ( result->action_contexts == NULL ) {
		result->action_contexts = hash_table_create
			(default_pool, result->pool, 0, NULL, NULL);
	} else {
		actctx = (struct sieve_result_action_context *) 
			hash_table_lookup(result->action_contexts, to_action);
	}

	if ( actctx == NULL ) {
		actctx = p_new
			(result->pool, struct sieve_result_action_context, 1);
		actctx->action = to_action;
		actctx->seffects = sieve_side_effects_list_create(result);
		
		hash_table_insert(result->action_contexts, (void *) to_action, 
			(void *) actctx);
	}	
	
	sieve_side_effects_list_add(actctx->seffects, seffect, context);
}

static int sieve_result_side_effects_merge
(const struct sieve_runtime_env *renv, const struct sieve_action *action, 
	struct sieve_side_effects_list *old_seffects,
	struct sieve_side_effects_list *new_seffects)
{
	int ret;
	struct sieve_result_side_effect *rsef, *nrsef;

	/* Allow side-effects to merge with existing copy */
		
	/* Merge existing side effects */
	rsef = old_seffects != NULL ? old_seffects->first_effect : NULL;
	while ( rsef != NULL ) {
		const struct sieve_side_effect *seffect = rsef->seffect;
		bool found = FALSE;
		
		if ( seffect->merge != NULL ) {

			/* Try to find it among the new */
			nrsef = new_seffects != NULL ? new_seffects->first_effect : NULL;
			while ( nrsef != NULL ) {
				if ( nrsef->seffect == seffect ) {
					if ( seffect->merge
						(renv, action, seffect, &rsef->context, nrsef->context) < 0 )
						return -1;
			
					found = TRUE;
					break;
				}
		
				nrsef = nrsef->next;
			}
	
			/* Not found? */
			if ( !found && seffect->merge
				(renv, action, seffect, &rsef->context, NULL) < 0 )
				return -1;
		}
	
		rsef = rsef->next;
	}

	/* Merge new Side effects */
	nrsef = new_seffects != NULL ? new_seffects->first_effect : NULL;
	while ( nrsef != NULL ) {
		const struct sieve_side_effect *seffect = nrsef->seffect;
		bool found = FALSE;
		
		if ( seffect->merge != NULL ) {
		
			/* Try to find it in among the exising */
			rsef = old_seffects != NULL ? old_seffects->first_effect : NULL;
			while ( rsef != NULL ) {
				if ( rsef->seffect == seffect ) {
					found = TRUE;
					break;
				}
				rsef = rsef->next;
			}
	
			/* Not found? */
			if ( !found ) {
				void *new_context = NULL; 
		
				if ( (ret=seffect->merge
					(renv, action, seffect, &new_context, nrsef->context)) < 0 ) 
					return -1;
					
				if ( ret != 0 ) {
					/* Add side effect */
					sieve_side_effects_list_add(new_seffects, seffect, new_context);
				}
			}
		}
	
		nrsef = nrsef->next;
	}
	
	return 1;
}

int sieve_result_add_action
(const struct sieve_runtime_env *renv,
	const struct sieve_action *action, struct sieve_side_effects_list *seffects,
	unsigned int source_line, void *context, unsigned int instance_limit)		
{
	int ret = 0;
	unsigned int instance_count = 0;
	struct sieve_result *result = renv->result;
	struct sieve_result_action *raction;
	const char *location = sieve_error_script_location
		(renv->script, source_line);
		
	/* First, check for duplicates or conflicts */
	raction = result->first_action;
	while ( raction != NULL ) {
		const struct sieve_action *oact = raction->action;
		
		if ( raction->action == action ) {
			instance_count++;

			/* Possible duplicate */
			if ( action->check_duplicate != NULL ) {
				if ( (ret=action->check_duplicate
					(renv, action, context, raction->context,
						location, raction->location)) < 0 )
					return ret;
				
				/* Duplicate */	
				if ( ret == 1 ) {
					return sieve_result_side_effects_merge
						(renv, action, raction->seffects, seffects);
				}
			}
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

	/* Check policy limit on total number of actions */
	if ( sieve_max_actions > 0 && result->action_count >= sieve_max_actions ) {
		sieve_runtime_error(renv, location, 
			"total number of actions exceeds policy limit");
		return -1;
	}

	/* Check policy limit on number of this class of actions */
	if ( instance_limit > 0 && instance_count >= instance_limit ) {
		sieve_runtime_error(renv, location, 
			"number of %s actions exceeds policy limit", action->name);
		return -1;
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
	result->action_count++;
	
	/* Apply any implicit side effects */
	if ( result->action_contexts != NULL ) {
		struct sieve_result_action_context *actctx;
		
		/* Check for implicit side effects to this particular action */
		actctx = (struct sieve_result_action_context *) 
				hash_table_lookup(result->action_contexts, action);
		
		if ( actctx != NULL ) {
			struct sieve_result_side_effect *iseff;
			
			/* Iterate through all implicit side effects and add those that are 
			 * missing.
			 */
			iseff = actctx->seffects->first_effect;
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

/*
 * Result execution
 */

static bool _sieve_result_implicit_keep
	(struct sieve_result *result, bool rollback)
{	
	bool success = TRUE;
	bool dummy = TRUE;
	struct act_store_context ctx;
	struct sieve_result_side_effect *rsef, *rsef_first = NULL;
	void *tr_context = NULL;
	
	ctx.folder = result->action_env.scriptenv->default_mailbox;
	
	/* Also apply any implicit side effects if applicable */
	if ( !rollback && result->action_contexts != NULL ) {
		struct sieve_result_action_context *actctx;
		
		/* Check for implicit side effects to store action */
		actctx = (struct sieve_result_action_context *) 
				hash_table_lookup(result->action_contexts, &act_store);
		
		if ( actctx != NULL && actctx->seffects != NULL ) 
			rsef_first = actctx->seffects->first_effect;
		
	}
	
	success = act_store.start(&act_store, &result->action_env, (void *) &ctx, &tr_context);

	if ( success ) {
		rsef = rsef_first;
		while ( success && rsef != NULL ) {
			const struct sieve_side_effect *sef = rsef->seffect;
			if ( sef->pre_execute != NULL ) 
				success = success && sef->pre_execute
					(sef, &act_store, &result->action_env, &rsef->context, tr_context);
			rsef = rsef->next;
		}

		success = success && act_store.execute
			(&act_store, &result->action_env, tr_context);

		rsef = rsef_first;
		while ( success && rsef != NULL ) {
			const struct sieve_side_effect *sef = rsef->seffect;
			if ( sef->post_execute != NULL ) 
				success = success && sef->post_execute
					(sef, &act_store, &result->action_env, rsef->context, tr_context);
			rsef = rsef->next;
		}
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

bool sieve_result_implicit_keep
(struct sieve_result *result, const struct sieve_message_data *msgdata,
	const struct sieve_script_env *senv, struct sieve_exec_status *estatus)
{
	result->action_env.msgdata = msgdata;
	result->action_env.scriptenv = senv;
	result->action_env.estatus = estatus;

	return _sieve_result_implicit_keep(result, TRUE);	
}

int sieve_result_execute
(struct sieve_result *result, const struct sieve_message_data *msgdata,
	const struct sieve_script_env *senv, struct sieve_exec_status *estatus)
{ 
	bool implicit_keep = TRUE;
	bool success = TRUE, commit_ok;
	struct sieve_result_action *rac;
	struct sieve_result_action *last_attempted;

	result->action_env.msgdata = msgdata;
	result->action_env.scriptenv = senv;
	result->action_env.estatus = estatus;
	
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
		} else {
			rac->tr_context = rac->context;
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
		
		/* Execute pre-execute event of side effects */
		rsef = rac->seffects != NULL ? rac->seffects->first_effect : NULL;
		while ( success && rsef != NULL ) {
			sef = rsef->seffect;
			if ( sef->pre_execute != NULL ) 
				success = success & sef->pre_execute
					(sef, act, &result->action_env, &rsef->context, rac->tr_context);
			rsef = rsef->next;
		}
	
		/* Execute the action itself */
		if ( success && act->execute != NULL ) {
			rac->success = act->execute(act, &result->action_env, rac->tr_context);
			success = success && rac->success;
		}
		
		/* Execute post-execute event of side effects */
		rsef = rac->seffects != NULL ? rac->seffects->first_effect : NULL;
		while ( success && rsef != NULL ) {
			sef = rsef->seffect;
			if ( sef->post_execute != NULL ) 
				success = success && sef->post_execute
					(sef, act, &result->action_env, rsef->context, rac->tr_context);
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
		
		if ( success ) {
			bool keep = TRUE;
		
			if ( act->commit != NULL ) 
				commit_ok = act->commit(act, &result->action_env, rac->tr_context, &keep) && 
					commit_ok;
	
			/* Execute post_commit event of side effects */
			rsef = rac->seffects != NULL ? rac->seffects->first_effect : NULL;
			while ( rsef != NULL ) {
				sef = rsef->seffect;
				if ( sef->post_commit != NULL ) 
					sef->post_commit
						(sef, act, &result->action_env, rsef->context, rac->tr_context, 
							&keep);
				rsef = rsef->next;
			}
			
			implicit_keep = implicit_keep && keep;
		} else {
			if ( act->rollback != NULL ) 
				act->rollback(act, &result->action_env, rac->tr_context, rac->success);
				
			/* Rollback side effects */
			rsef = rac->seffects != NULL ? rac->seffects->first_effect : NULL;
			while ( rsef != NULL ) {
				sef = rsef->seffect;
				if ( sef->rollback != NULL ) 
					sef->rollback
						(sef, act, &result->action_env, rsef->context, rac->tr_context, 
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
		if ( !_sieve_result_implicit_keep(result, !commit_ok) ) 
			return SIEVE_EXEC_KEEP_FAILED;
			
		return ( commit_ok ? 
			SIEVE_EXEC_OK            /* Success */ :
			SIEVE_EXEC_FAILURE       /* Implicit keep executed */ );
	}
	
	/* Unconditional success */
	return SIEVE_EXEC_OK;
}

/*
 * Result evaluation
 */

struct sieve_result_iterate_context {
	struct sieve_result *result;
	struct sieve_result_action *action;
};

struct sieve_result_iterate_context *sieve_result_iterate_init
(struct sieve_result *result)
{
	struct sieve_result_iterate_context *rictx = 
		t_new(struct sieve_result_iterate_context, 1);
	
	rictx->result = result;
	rictx->action = result->first_action;

	return rictx;
}

const struct sieve_action *sieve_result_iterate_next
	(struct sieve_result_iterate_context *rictx, void **context)
{
	struct sieve_result_action *act;

	if ( rictx == NULL )
		return  NULL;

	act = rictx->action;
	if ( act != NULL ) {
		rictx->action = act->next;

		if ( context != NULL )
			*context = act->context;
	
		return act->action;
	}

	return NULL;
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



