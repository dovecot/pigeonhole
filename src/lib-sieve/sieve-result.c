/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
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
	struct sieve_action_data data;
	
	void *tr_context;
	bool success;
	
	bool keep;

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
	
	const struct sieve_action *keep_action;

	unsigned int action_count;
	struct sieve_result_action *first_action;
	struct sieve_result_action *last_action;
	
	struct sieve_result_action *last_attempted_action;
	
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
		
	result->keep_action = &act_store;
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

struct sieve_error_handler *sieve_result_get_error_handler
(struct sieve_result *result)
{
	return result->ehandler;
}

/*
 * Extension support
 */

void sieve_result_extension_set_context
(struct sieve_result *result, const struct sieve_extension *ext, void *context)
{
	array_idx_set(&result->ext_contexts, (unsigned int) SIEVE_EXT_ID(ext), 
		&context);	
}

const void *sieve_result_extension_get_context
(struct sieve_result *result, const struct sieve_extension *ext) 
{
	int ext_id = SIEVE_EXT_ID(ext);
	void * const *ctx;

	if  ( ext_id < 0 || ext_id >= (int) array_count(&result->ext_contexts) )
		return NULL;
	
	ctx = array_idx(&result->ext_contexts, (unsigned int) ext_id);		

	return *ctx;
}

/* 
 * Error handling 
 */

void sieve_result_error
	(const struct sieve_action_exec_env *aenv, const char *fmt, ...)
{
	va_list args;
	
	va_start(args, fmt);	
	sieve_verror(aenv->result->ehandler, sieve_action_get_location(aenv), fmt, args); 
	va_end(args);
}

void sieve_result_warning
	(const struct sieve_action_exec_env *aenv, const char *fmt, ...)
{
	va_list args;
	
	va_start(args, fmt);	
	sieve_vwarning(aenv->result->ehandler, sieve_action_get_location(aenv), fmt, args); 
	va_end(args);
}

void sieve_result_log
	(const struct sieve_action_exec_env *aenv, const char *fmt, ...)
{
	va_list args;
	
	va_start(args, fmt);	
	sieve_vinfo(aenv->result->ehandler, sieve_action_get_location(aenv), fmt, args); 
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
		
			/* Try to find it among the exising */
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
					sieve_side_effects_list_add(old_seffects, seffect, new_context);
				}
			}
		}
	
		nrsef = nrsef->next;
	}
	
	return 1;
}

static int _sieve_result_add_action
(const struct sieve_runtime_env *renv,
	const struct sieve_action *action, struct sieve_side_effects_list *seffects,
	unsigned int source_line, void *context, unsigned int instance_limit, 
	bool keep)		
{
	int ret = 0;
	unsigned int instance_count = 0;
	struct sieve_result *result = renv->result;
	struct sieve_result_action *raction = NULL, *kaction = NULL;
	struct sieve_action_data act_data;
			
	act_data.action = action;
	act_data.location = sieve_error_script_location(renv->script, source_line);
	act_data.context = context;
	act_data.executed = FALSE;
		
	/* First, check for duplicates or conflicts */
	raction = result->first_action;
	while ( raction != NULL ) {
		const struct sieve_action *oact = raction->data.action;
		
		if ( keep && raction->keep ) {
		
			/* Duplicate keep */
			if ( raction->data.executed ) {
				/* Keep action from preceeding execution */
			
				kaction = raction;
			
				/* Detach existing keep action */
				if ( result->first_action == kaction ) 
					result->first_action = kaction->next;
				if ( result->last_action == kaction ) 
					result->last_action = kaction->prev;
				if ( kaction->next != NULL ) kaction->next->prev = kaction->prev;
				if ( kaction->prev != NULL ) kaction->prev->next = kaction->next;
			
				/* Merge existing side-effects with new keep action */
				if ( raction->data.action == NULL ) {
					if ( (ret=sieve_result_side_effects_merge
						(renv, action, seffects, kaction->seffects)) <= 0 )	 
						return ret;
				}
				
			} else {
				/* True duplicate */
				
				return sieve_result_side_effects_merge
					(renv, action, raction->seffects, seffects);
			}
			
		} if ( raction->data.action == action ) {
			instance_count++;

			/* Possible duplicate */
			if ( action->check_duplicate != NULL ) {
				if ( (ret=action->check_duplicate(renv, &act_data, &raction->data)) 
					< 0 )
					return ret;
				
				/* Duplicate */	
				if ( ret == 1 ) {
					if ( keep && !raction->keep ) {
						/* New keep has higher precedence than existing duplicate non-keep 
						 * action. So, take over the result action object and transform it
						 * into a keep.
						 */
						if ( (ret=sieve_result_side_effects_merge
							(renv, action, raction->seffects, seffects)) < 0 ) 
							return ret;
							
						raction->keep = TRUE;
						raction->data.context = NULL;
						raction->data.location = p_strdup(result->pool, act_data.location);
						
						/* Note that existing execution status is retained, making sure that
						 * keep is not executed multiple times.
						 */
						
						return 1;
					} else {
						/* Merge side-effects, but don't add new action */
						return sieve_result_side_effects_merge
							(renv, action, raction->seffects, seffects);
					}
				}
			}
		} else {
			/* Check conflict */
			if ( action->check_conflict != NULL &&
				(ret=action->check_conflict(renv, &act_data, &raction->data)) != 0 ) 
				return ret;
			
			if ( oact->check_conflict != NULL &&
				(ret=oact->check_conflict(renv, &raction->data, &act_data)) != 0 )
				return ret;
		}
		raction = raction->next;
	}

	/* Check policy limit on total number of actions */
	if ( sieve_max_actions > 0 && result->action_count >= sieve_max_actions ) {
		sieve_runtime_error(renv, act_data.location, 
			"total number of actions exceeds policy limit");
		return -1;
	}

	/* Check policy limit on number of this class of actions */
	if ( instance_limit > 0 && instance_count >= instance_limit ) {
		sieve_runtime_error(renv, act_data.location, 
			"number of %s actions exceeds policy limit", action->name);
		return -1;
	}	
		
	if ( kaction != NULL )
		/* Use existing keep action to define new one */
		raction = kaction;
	else {
		/* Create new action object */
		raction = p_new(result->pool, struct sieve_result_action, 1);
	}
	
	raction->result = result;
	raction->data.context =	context;
	raction->data.action = action;
	raction->data.location = p_strdup(result->pool, act_data.location);
	raction->data.executed = FALSE;
	raction->tr_context = NULL;
	raction->success = FALSE;
	raction->keep = keep;
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

int sieve_result_add_action
(const struct sieve_runtime_env *renv,
	const struct sieve_action *action, struct sieve_side_effects_list *seffects,
	unsigned int source_line, void *context, unsigned int instance_limit)
{
	return _sieve_result_add_action
		(renv, action, seffects, source_line, context, instance_limit, FALSE);
}

int sieve_result_add_keep
(const struct sieve_runtime_env *renv, struct sieve_side_effects_list *seffects,
	unsigned int source_line)
{
	return _sieve_result_add_action
		(renv, renv->result->keep_action, seffects, source_line, NULL, 0, TRUE);
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
(struct sieve_result *result, const struct sieve_script_env *senv, 
	struct ostream *stream)
{
	struct sieve_result_print_env penv;
	bool implicit_keep = TRUE;
	struct sieve_result_action *rac;
	
	penv.result = result;
	penv.stream = stream;
	penv.scriptenv = senv;
	
	sieve_result_printf(&penv, "\nPerformed actions:\n\n");
	rac = result->first_action;
	while ( rac != NULL ) {		
		bool keep = TRUE;
		struct sieve_result_side_effect *rsef;
		const struct sieve_side_effect *sef;
		const struct sieve_action *act = rac->data.action;

		if ( act != NULL ) {
			if ( act->print != NULL )
				act->print(act, &penv, rac->data.context, &keep);
			else
				sieve_result_action_printf(&penv, act->name); 
		} else {
			if ( rac->keep ) {
				sieve_result_action_printf(&penv, "keep");
				keep = FALSE;
			} else {
				sieve_result_action_printf(&penv, "[NULL]");
			}
		}
	
		/* Print side effects */
		rsef = rac->seffects != NULL ? rac->seffects->first_effect : NULL;
		while ( rsef != NULL ) {
			sef = rsef->seffect;
			if ( sef->print != NULL ) 
				sef->print(sef, rac->data.action, &penv, rsef->context, &keep);
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
	struct sieve_result_side_effect *rsef, *rsef_first = NULL;
	void *tr_context = NULL;
	const struct sieve_action *act_keep = result->keep_action;
	
	/* If keep is a non-action, return right away */
	if ( act_keep == NULL ) return TRUE; 
		
	/* Apply any implicit side effects if applicable */
	if ( !rollback && result->action_contexts != NULL ) {
		struct sieve_result_action_context *actctx;
		
		/* Check for implicit side effects to keep action */
		actctx = (struct sieve_result_action_context *) 
				hash_table_lookup(result->action_contexts, act_keep);
		
		if ( actctx != NULL && actctx->seffects != NULL ) 
			rsef_first = actctx->seffects->first_effect;
	}
	
	/* Start keep action */
	if ( act_keep->start != NULL ) 
		success = act_keep->start
			(act_keep, &result->action_env, NULL, &tr_context);

	/* Execute keep action */
	if ( success ) {
		rsef = rsef_first;
		while ( success && rsef != NULL ) {
			const struct sieve_side_effect *sef = rsef->seffect;
			if ( sef->pre_execute != NULL ) 
				success = success && sef->pre_execute
					(sef, act_keep, &result->action_env, &rsef->context, tr_context);
			rsef = rsef->next;
		}

		if ( act_keep->execute != NULL )
			success = success && act_keep->execute
				(act_keep, &result->action_env, tr_context);

		rsef = rsef_first;
		while ( success && rsef != NULL ) {
			const struct sieve_side_effect *sef = rsef->seffect;
			if ( sef->post_execute != NULL ) 
				success = success && sef->post_execute
					(sef, act_keep, &result->action_env, rsef->context, tr_context);
			rsef = rsef->next;
		}
	}
	
	/* Finish keep action */
	if ( success ) {
		if ( act_keep->commit != NULL ) 
			success = act_keep->commit
				(act_keep, &result->action_env, tr_context, &dummy);

		rsef = rsef_first;
		while ( rsef != NULL ) {
			const struct sieve_side_effect *sef = rsef->seffect;
			bool keep = TRUE;
			
			if ( sef->post_commit != NULL ) 
				sef->post_commit
					(sef, act_keep, &result->action_env, rsef->context, tr_context, 
						&keep);
			rsef = rsef->next;
		}
			
		return success; 
	}
	
	/* Failed, rollback */
	if ( act_keep->rollback != NULL )
		act_keep->rollback(act_keep, &result->action_env, tr_context, success);

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
	struct sieve_result_action *rac, *first_action;
	struct sieve_result_action *last_attempted;

	/* Prepare environment */

	result->action_env.msgdata = msgdata;
	result->action_env.scriptenv = senv;
	result->action_env.estatus = estatus;
	
	/* Make notice of this attempt */
	
	first_action = ( result->last_attempted_action == NULL ?
		result->first_action : result->last_attempted_action );
	result->last_attempted_action = result->last_action;
	
	/* 
	 * Transaction start 
	 */
	
	rac = first_action;
	while ( success && rac != NULL ) {
		const struct sieve_action *act = rac->data.action;
	
		/* Skip non-action (inactive keep) */
		if ( act == NULL ) continue;
	
		if ( act->start != NULL ) {
			rac->success = act->start(act, &result->action_env, rac->data.context, 
				&rac->tr_context);
			success = success && rac->success;
		} else {
			rac->tr_context = rac->data.context;
		}
 
		rac = rac->next;	
	}
	
	/* 
	 * Transaction execute 
	 */
	
	last_attempted = rac;
	rac = first_action;
	while ( success && rac != NULL ) {
		const struct sieve_action *act = rac->data.action;
		struct sieve_result_side_effect *rsef;
		const struct sieve_side_effect *sef;
		
		/* Skip non-action (inactive keep) */
		if ( act == NULL ) continue;
		
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
	rac = first_action;
	while ( rac != NULL && rac != last_attempted ) {
		const struct sieve_action *act = rac->data.action;
		struct sieve_result_side_effect *rsef;
		const struct sieve_side_effect *sef;
		
		if ( success ) {
			bool keep = TRUE;
			
			rac->data.executed = TRUE;
			
			/* Skip non-action (inactive keep) */
			if ( act == NULL ) continue;
		
			if ( act->commit != NULL ) 
				commit_ok = act->commit
					(act, &result->action_env, rac->tr_context, &keep) && commit_ok;
	
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
			/* Skip non-action (inactive keep) */
			if ( act == NULL ) continue;
		
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
	(struct sieve_result_iterate_context *rictx, bool *keep, void **context)
{
	struct sieve_result_action *rac;

	if ( rictx == NULL )
		return  NULL;

	rac = rictx->action;
	if ( rac != NULL ) {
		rictx->action = rac->next;
		
		if ( keep != NULL )
			*keep = rac->keep;

		if ( context != NULL )
			*context = rac->data.context;
	
		return rac->data.action;
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



