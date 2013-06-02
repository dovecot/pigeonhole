/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "mempool.h"
#include "ostream.h"
#include "hash.h"
#include "str.h"
#include "strfuncs.h"
#include "str-sanitize.h"
#include "var-expand.h"
#include "message-address.h"
#include "mail-deliver.h"

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-script.h"
#include "sieve-error.h"
#include "sieve-interpreter.h"
#include "sieve-actions.h"
#include "sieve-message.h"

#include "sieve-result.h"

#include <stdio.h>

/*
 * Defaults
 */

#define DEFAULT_ACTION_LOG_FORMAT "msgid=%m: %$"

/*
 * Types
 */

struct sieve_result_action {
	struct sieve_action action;

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
	struct sieve_side_effect seffect;

	struct sieve_result_side_effect *prev, *next;
};

struct sieve_result_action_context {
	const struct sieve_action_def *action;
	struct sieve_side_effects_list *seffects;
};

/*
 * Result object
 */

struct sieve_result {
	pool_t pool;
	int refcount;

	struct sieve_instance *svinst;

	/* Context data for extensions */
	ARRAY(void *) ext_contexts;

	struct sieve_error_handler *ehandler;

	struct sieve_action_exec_env action_env;

	struct sieve_action keep_action;
	struct sieve_action failure_action;

	unsigned int action_count;
	struct sieve_result_action *first_action;
	struct sieve_result_action *last_action;

	struct sieve_result_action *last_attempted_action;

	HASH_TABLE(const struct sieve_action_def *,
			   struct sieve_result_action_context *) action_contexts;

	unsigned int executed:1;
};

struct sieve_result *sieve_result_create
(struct sieve_instance *svinst, const struct sieve_message_data *msgdata,
	const struct sieve_script_env *senv, struct sieve_error_handler *ehandler)
{
	pool_t pool;
	struct sieve_result *result;

	pool = pool_alloconly_create("sieve_result", 4096);
	result = p_new(pool, struct sieve_result, 1);
	result->refcount = 1;
	result->pool = pool;
	result->svinst = svinst;

	p_array_init(&result->ext_contexts, pool, 4);

	if ( ehandler != NULL )
		sieve_error_handler_ref(ehandler);
	result->ehandler = ehandler;

	result->action_env.svinst = svinst;
	result->action_env.result = result;
	result->action_env.scriptenv = senv;
	result->action_env.msgdata = msgdata;
	result->action_env.msgctx = sieve_message_context_create
		(svinst, senv->user, msgdata);

	result->keep_action.def = &act_store;
	result->keep_action.ext = NULL;
	result->failure_action.def = &act_store;
	result->failure_action.ext = NULL;

	result->action_count = 0;
	result->first_action = NULL;
	result->last_action = NULL;

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

	sieve_message_context_unref(&(*result)->action_env.msgctx);

	if ( hash_table_is_created((*result)->action_contexts) )
        hash_table_destroy(&(*result)->action_contexts);

	if ( (*result)->ehandler != NULL )
		sieve_error_handler_unref(&(*result)->ehandler);

	if ( (*result)->action_env.ehandler != NULL )
		sieve_error_handler_unref(&(*result)->action_env.ehandler);

	pool_unref(&(*result)->pool);

 	*result = NULL;
}

pool_t sieve_result_pool(struct sieve_result *result)
{
	return result->pool;
}

/*
 * Getters/Setters
 */

struct sieve_error_handler *sieve_result_get_error_handler
(struct sieve_result *result)
{
	return result->ehandler;
}
const struct sieve_script_env *sieve_result_get_script_env
(struct sieve_result *result)
{
    return result->action_env.scriptenv;
}

const struct sieve_message_data *sieve_result_get_message_data
(struct sieve_result *result)
{
	return result->action_env.msgdata;
}

struct sieve_message_context *sieve_result_get_message_context
(struct sieve_result *result)
{
	return result->action_env.msgctx;
}

void sieve_result_set_error_handler
(struct sieve_result *result, struct sieve_error_handler *ehandler)
{
	if ( result->ehandler != ehandler ) {
		sieve_error_handler_ref(ehandler);
		sieve_error_handler_unref(&result->ehandler);
		result->ehandler = ehandler;
	}
}

/*
 * Extension support
 */

void sieve_result_extension_set_context
(struct sieve_result *result, const struct sieve_extension *ext, void *context)
{
	if ( ext->id < 0 ) return;

	array_idx_set(&result->ext_contexts, (unsigned int) ext->id, &context);
}

const void *sieve_result_extension_get_context
(struct sieve_result *result, const struct sieve_extension *ext)
{
	void * const *ctx;

	if  ( ext->id < 0 || ext->id >= (int) array_count(&result->ext_contexts) )
		return NULL;

	ctx = array_idx(&result->ext_contexts, (unsigned int) ext->id);

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
	sieve_verror(aenv->ehandler, NULL, fmt, args);
	va_end(args);
}

void sieve_result_global_error
(const struct sieve_action_exec_env *aenv, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	sieve_global_verror(aenv->svinst, aenv->ehandler, NULL, fmt, args);
	va_end(args);
}

void sieve_result_warning
(const struct sieve_action_exec_env *aenv, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	sieve_vwarning(aenv->ehandler, NULL, fmt, args);
	va_end(args);
}

void sieve_result_global_warning
(const struct sieve_action_exec_env *aenv, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	sieve_global_vwarning(aenv->svinst, aenv->ehandler, NULL, fmt, args);
	va_end(args);
}

void sieve_result_log
(const struct sieve_action_exec_env *aenv, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	sieve_vinfo(aenv->ehandler, NULL, fmt, args);
	va_end(args);
}

void sieve_result_global_log
(const struct sieve_action_exec_env *aenv, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	sieve_global_vinfo(aenv->svinst, aenv->ehandler, NULL, fmt, args);
	va_end(args);
}

void sieve_result_global_log_error
(const struct sieve_action_exec_env *aenv, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	sieve_global_info_verror(aenv->svinst, aenv->ehandler, NULL, fmt, args);
	va_end(args);
}

void sieve_result_global_log_warning
(const struct sieve_action_exec_env *aenv, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	sieve_global_info_vwarning(aenv->svinst, aenv->ehandler, NULL, fmt, args);
	va_end(args);
}

/*
 * Result composition
 */

void sieve_result_add_implicit_side_effect
(struct sieve_result *result, const struct sieve_action_def *to_action,
	bool to_keep, const struct sieve_extension *ext,
	const struct sieve_side_effect_def *seff_def, void *context)
{
	struct sieve_result_action_context *actctx = NULL;
	struct sieve_side_effect seffect;

	to_action = to_keep ? &act_store : to_action;

	if ( !hash_table_is_created(result->action_contexts) ) {
		hash_table_create_direct(&result->action_contexts, result->pool, 0);
	} else {
		actctx = hash_table_lookup(result->action_contexts, to_action);
	}

	if ( actctx == NULL ) {
		actctx = p_new
			(result->pool, struct sieve_result_action_context, 1);
		actctx->action = to_action;
		actctx->seffects = sieve_side_effects_list_create(result);

		hash_table_insert(result->action_contexts, to_action, actctx);
	}

	seffect.object.def = &seff_def->obj_def;
	seffect.object.ext = ext;
	seffect.def = seff_def;
	seffect.context = context;

	sieve_side_effects_list_add(actctx->seffects, &seffect);
}

static int sieve_result_side_effects_merge
(const struct sieve_runtime_env *renv, const struct sieve_action *action,
	struct sieve_result_action *old_action,
	struct sieve_side_effects_list *new_seffects)
{
	struct sieve_side_effects_list *old_seffects = old_action->seffects;
	int ret;
	struct sieve_result_side_effect *rsef, *nrsef;

	/* Allow side-effects to merge with existing copy */

	/* Merge existing side effects */
	rsef = old_seffects != NULL ? old_seffects->first_effect : NULL;
	while ( rsef != NULL ) {
		struct sieve_side_effect *seffect = &rsef->seffect;
		bool found = FALSE;

		if ( seffect->def != NULL && seffect->def->merge != NULL ) {

			/* Try to find it among the new */
			nrsef = new_seffects != NULL ? new_seffects->first_effect : NULL;
			while ( nrsef != NULL ) {
				struct sieve_side_effect *nseffect = &nrsef->seffect;

				if ( nseffect->def == seffect->def ) {
					if ( seffect->def->merge
						(renv, action, seffect, nseffect, &seffect->context) < 0 )
						return -1;

					found = TRUE;
					break;
				}

				nrsef = nrsef->next;
			}

			/* Not found? */
			if ( !found && seffect->def->merge
				(renv, action, seffect, NULL, &rsef->seffect.context) < 0 )
				return -1;
		}

		rsef = rsef->next;
	}

	/* Merge new Side effects */
	nrsef = new_seffects != NULL ? new_seffects->first_effect : NULL;
	while ( nrsef != NULL ) {
		struct sieve_side_effect *nseffect = &nrsef->seffect;
		bool found = FALSE;

		if ( nseffect->def != NULL && nseffect->def->merge != NULL ) {

			/* Try to find it among the exising */
			rsef = old_seffects != NULL ? old_seffects->first_effect : NULL;
			while ( rsef != NULL ) {
				if ( rsef->seffect.def == nseffect->def ) {
					found = TRUE;
					break;
				}
				rsef = rsef->next;
			}

			/* Not found? */
			if ( !found ) {
				void *new_context = NULL;

				if ( (ret=nseffect->def->merge
					(renv, action, nseffect, nseffect, &new_context)) < 0 )
					return -1;

				if ( ret != 0 ) {
					if ( old_action->seffects == NULL )
						old_action->seffects = old_seffects =
							sieve_side_effects_list_create(renv->result);

					nseffect->context = new_context;

					/* Add side effect */
					sieve_side_effects_list_add(old_seffects, nseffect);
				}
			}
		}

		nrsef = nrsef->next;
	}

	return 1;
}

static void sieve_result_action_detach
(struct sieve_result *result, struct sieve_result_action *raction)
{
	if ( result->first_action == raction )
		result->first_action = raction->next;

	if ( result->last_action == raction )
		result->last_action = raction->prev;

	if ( result->last_attempted_action == raction )
		result->last_attempted_action = raction->prev;

	if ( raction->next != NULL ) raction->next->prev = raction->prev;
	if ( raction->prev != NULL ) raction->prev->next = raction->next;

	raction->next = NULL;
	raction->prev = NULL;

	if ( result->action_count > 0 )
		result->action_count--;
}

static int _sieve_result_add_action
(const struct sieve_runtime_env *renv, const struct sieve_extension *ext,
	const struct sieve_action_def *act_def,
	struct sieve_side_effects_list *seffects,
	void *context, unsigned int instance_limit, bool preserve_mail, bool keep)
{
	int ret = 0;
	unsigned int instance_count = 0;
	struct sieve_instance *svinst = renv->svinst;
	struct sieve_result *result = renv->result;
	struct sieve_result_action *raction = NULL, *kaction = NULL;
	struct sieve_action action;

	action.def = act_def;
	action.ext = ext;
	action.location = sieve_runtime_get_full_command_location(renv);
	action.context = context;
	action.executed = FALSE;

	/* First, check for duplicates or conflicts */
	raction = result->first_action;
	while ( raction != NULL ) {
		const struct sieve_action *oact = &raction->action;

		if ( keep && raction->keep ) {

			/* Duplicate keep */
			if ( raction->action.def == NULL || raction->action.executed ) {
				/* Keep action from preceeding execution */

				/* Detach existing keep action */
				sieve_result_action_detach(result, raction);

				/* Merge existing side-effects with new keep action */
				if ( kaction == NULL )
					kaction = raction;

				if ( (ret=sieve_result_side_effects_merge
					(renv, &action, kaction, seffects)) <= 0 )
					return ret;
			} else {
				/* True duplicate */
				return sieve_result_side_effects_merge
					(renv, &action, raction, seffects);
			}

		} if ( act_def != NULL && raction->action.def == act_def ) {
			instance_count++;

			/* Possible duplicate */
			if ( act_def->check_duplicate != NULL ) {
				if ( (ret=act_def->check_duplicate(renv, &action, &raction->action))
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
							(renv, &action, raction, seffects)) < 0 )
							return ret;

						if ( kaction == NULL ) {
							raction->action.context = NULL;
							raction->action.location =
								p_strdup(result->pool, action.location);

							/* Note that existing execution status is retained, making sure
							 * that keep is not executed multiple times.
							 */

							kaction = raction;

						} else {
							sieve_result_action_detach(result, raction);

							if ( (ret=sieve_result_side_effects_merge
								(renv, &action, kaction, raction->seffects)) < 0 )
								return ret;
						}
					} else {
						/* Merge side-effects, but don't add new action */
						return sieve_result_side_effects_merge
							(renv, &action, raction, seffects);
					}
				}
			}
		} else {
			if ( act_def != NULL && oact->def != NULL ) {
				/* Check conflict */
				if ( act_def->check_conflict != NULL &&
					(ret=act_def->check_conflict(renv, &action, &raction->action)) != 0 )
					return ret;

				if ( !raction->action.executed && oact->def->check_conflict != NULL &&
					(ret=oact->def->check_conflict
						(renv, &raction->action, &action)) != 0 )
					return ret;
			}
		}
		raction = raction->next;
	}

	if ( kaction != NULL ) {
		/* Use existing keep action to define new one */
		raction = kaction;
	} else {
		/* Check policy limit on total number of actions */
		if ( svinst->max_actions > 0 && result->action_count >= svinst->max_actions )
		{
			sieve_runtime_error(renv, action.location,
				"total number of actions exceeds policy limit (%u > %u)",
				result->action_count+1, svinst->max_actions);
			return -1;
		}

		/* Check policy limit on number of this class of actions */
		if ( instance_limit > 0 && instance_count >= instance_limit ) {
			sieve_runtime_error(renv, action.location,
				"number of %s actions exceeds policy limit (%u > %u)",
				act_def->name, instance_count+1, instance_limit);
			return -1;
		}

		/* Create new action object */
		raction = p_new(result->pool, struct sieve_result_action, 1);
		raction->action.executed = FALSE;
		raction->seffects = seffects;
		raction->tr_context = NULL;
		raction->success = FALSE;
	}

	raction->action.context = context;
	raction->action.def = act_def;
	raction->action.ext = ext;
	raction->action.location = p_strdup(result->pool, action.location);
	raction->keep = keep;

	if ( raction->prev == NULL && raction != result->first_action ) {
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
		if ( hash_table_is_created(result->action_contexts) ) {
			struct sieve_result_action_context *actctx;

			/* Check for implicit side effects to this particular action */
			actctx = hash_table_lookup(result->action_contexts,
					( keep ? &act_store : act_def ));

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
							if ( seff->seffect.def == iseff->seffect.def ) {
								exists = TRUE;
								break;
							}

							seff = seff->next;
						}
					} else {
						raction->seffects = seffects =
							sieve_side_effects_list_create(result);
					}

					/* If not present, add it */
					if ( !exists ) {
						sieve_side_effects_list_add(seffects, &iseff->seffect);
					}

					iseff = iseff->next;
				}
			}
		}
	}

	if ( preserve_mail ) {
		raction->action.mail = sieve_message_get_mail(renv->msgctx);
		sieve_message_snapshot(renv->msgctx);
	} else {
		raction->action.mail = NULL;
	}

	return 0;
}

int sieve_result_add_action
(const struct sieve_runtime_env *renv, const struct sieve_extension *ext,
	const struct sieve_action_def *act_def,
	struct sieve_side_effects_list *seffects,	void *context,
	unsigned int instance_limit, bool preserve_mail)
{
	return _sieve_result_add_action(renv, ext, act_def, seffects, context,
		instance_limit, preserve_mail, FALSE);
}

int sieve_result_add_keep
(const struct sieve_runtime_env *renv, struct sieve_side_effects_list *seffects)
{
	return _sieve_result_add_action
		(renv, renv->result->keep_action.ext, renv->result->keep_action.def,
			seffects, NULL, 0, TRUE, TRUE);
}

void sieve_result_set_keep_action
(struct sieve_result *result, const struct sieve_extension *ext,
	const struct sieve_action_def *act_def)
{
	result->keep_action.def = act_def;
	result->keep_action.ext = ext;
}

void sieve_result_set_failure_action
(struct sieve_result *result, const struct sieve_extension *ext,
	const struct sieve_action_def *act_def)
{
	result->failure_action.def = act_def;
	result->failure_action.ext = ext;
}

/*
 * Result printing
 */

void sieve_result_vprintf
(const struct sieve_result_print_env *penv, const char *fmt, va_list args)
{
	string_t *outbuf = t_str_new(128);

	str_vprintfa(outbuf, fmt, args);

	o_stream_send(penv->stream, str_data(outbuf), str_len(outbuf));
}

void sieve_result_printf
(const struct sieve_result_print_env *penv, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	sieve_result_vprintf(penv, fmt, args);
	va_end(args);
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

static void sieve_result_print_side_effects
(struct sieve_result_print_env *rpenv, const struct sieve_action *action,
	struct sieve_side_effects_list *slist, bool *implicit_keep)
{
	struct sieve_result_side_effect *rsef;

	/* Print side effects */
	rsef = slist != NULL ? slist->first_effect : NULL;
	while ( rsef != NULL ) {
		if ( rsef->seffect.def != NULL ) {
			const struct sieve_side_effect *sef = &rsef->seffect;

			if ( sef->def->print != NULL )
				sef->def->print(sef, action, rpenv, implicit_keep);
		}
		rsef = rsef->next;
	}
}

static void sieve_result_print_implicit_side_effects
(struct sieve_result_print_env *rpenv)
{
	struct sieve_result *result = rpenv->result;
	bool dummy = TRUE;

	/* Print any implicit side effects if applicable */
	if ( hash_table_is_created(result->action_contexts) ) {
		struct sieve_result_action_context *actctx;

		/* Check for implicit side effects to keep action */
		actctx = hash_table_lookup(rpenv->result->action_contexts, &act_store);

		if ( actctx != NULL && actctx->seffects != NULL )
			sieve_result_print_side_effects
				(rpenv, &result->keep_action, actctx->seffects, &dummy);
	}
}

bool sieve_result_print
(struct sieve_result *result, const struct sieve_script_env *senv,
	struct ostream *stream, bool *keep)
{
	struct sieve_action act_keep = result->keep_action;
	struct sieve_result_print_env penv;
	bool implicit_keep = TRUE;
	struct sieve_result_action *rac, *first_action;

	first_action = ( result->last_attempted_action == NULL ?
		result->first_action : result->last_attempted_action->next );

	if ( keep != NULL ) *keep = FALSE;

	/* Prepare environment */

	penv.result = result;
	penv.stream = stream;
	penv.scriptenv = senv;

	sieve_result_printf(&penv, "\nPerformed actions:\n\n");

	if ( first_action == NULL ) {
		sieve_result_printf(&penv, "  (none)\n");
	} else {
		rac = first_action;
		while ( rac != NULL ) {
			bool impl_keep = TRUE;
			const struct sieve_action *act = &rac->action;

			if ( rac->keep && keep != NULL ) *keep = TRUE;

			if ( act->def != NULL ) {
				if ( act->def->print != NULL )
					act->def->print(act, &penv, &impl_keep);
				else
					sieve_result_action_printf(&penv, "%s", act->def->name);
			} else {
				if ( rac->keep ) {
					sieve_result_action_printf(&penv, "keep");
					impl_keep = FALSE;
				} else {
					sieve_result_action_printf(&penv, "[NULL]");
				}
			}

			/* Print side effects */
			sieve_result_print_side_effects
				(&penv, &rac->action, rac->seffects, &impl_keep);

			implicit_keep = implicit_keep && impl_keep;

			rac = rac->next;
		}
	}

	if ( implicit_keep && keep != NULL ) *keep = TRUE;

	sieve_result_printf(&penv, "\nImplicit keep:\n\n");

	if ( implicit_keep ) {
		bool dummy = TRUE;

		if ( act_keep.def == NULL ) {
			sieve_result_action_printf(&penv, "keep");

			sieve_result_print_implicit_side_effects(&penv);
		} else {
			/* Scan for execution of keep-equal actions */
			rac = result->first_action;
			while ( act_keep.def != NULL && rac != NULL ) {
				if ( rac->action.def == act_keep.def && act_keep.def->equals != NULL
					&& act_keep.def->equals(senv, NULL, &rac->action)
						&& rac->action.executed ) {
					act_keep.def = NULL;
				}

				rac = rac->next;
			}

			if ( act_keep.def == NULL ) {
				sieve_result_printf(&penv,
					"  (none; keep or equivalent action executed earlier)\n");
			} else {
				act_keep.def->print(&act_keep, &penv, &dummy);

				sieve_result_print_implicit_side_effects(&penv);
			}
		}
	} else
		sieve_result_printf(&penv, "  (none)\n");

	sieve_result_printf(&penv, "\n");

	return TRUE;
}

/*
 * Result execution
 */

static void _sieve_result_prepare_execution(struct sieve_result *result)
{
	const struct sieve_message_data *msgdata = result->action_env.msgdata;
	const struct sieve_script_env *senv = result->action_env.scriptenv;
	const struct var_expand_table *tab;

	tab = mail_deliver_get_log_var_expand_table(msgdata->mail, NULL);
	result->action_env.exec_status =
		( senv->exec_status == NULL ?
			t_new(struct sieve_exec_status, 1) : senv->exec_status );

	if ( result->action_env.ehandler != NULL )
		sieve_error_handler_unref(&result->action_env.ehandler);

	if ( senv->action_log_format != NULL ) {
		result->action_env.ehandler = sieve_varexpand_ehandler_create
			(result->ehandler, senv->action_log_format, tab);
	} else {
		result->action_env.ehandler = sieve_varexpand_ehandler_create
            (result->ehandler, DEFAULT_ACTION_LOG_FORMAT, tab);
	}
}

static int _sieve_result_implicit_keep
(struct sieve_result *result, bool rollback)
{
	struct sieve_result_action *rac;
	int status = SIEVE_EXEC_OK;
	struct sieve_result_side_effect *rsef, *rsef_first = NULL;
	void *tr_context = NULL;
	struct sieve_action act_keep;

	if ( rollback ) {
		act_keep = result->failure_action;
		act_keep.mail = NULL;
	} else {
		act_keep = result->keep_action;
		act_keep.mail = sieve_message_get_mail(result->action_env.msgctx);
	}

	/* If keep is a non-action, return right away */
	if ( act_keep.def == NULL ) return SIEVE_EXEC_OK;

	/* Scan for execution of keep-equal actions */
	rac = result->first_action;
	while ( rac != NULL ) {
		if ( rac->action.def == act_keep.def && act_keep.def->equals != NULL &&
			act_keep.def->equals
				(result->action_env.scriptenv, NULL, &rac->action) &&
					rac->action.executed )
			return SIEVE_EXEC_OK;

		rac = rac->next;
	}

	/* Apply any implicit side effects if applicable */
	if ( !rollback && hash_table_is_created(result->action_contexts) ) {
		struct sieve_result_action_context *actctx;

		/* Check for implicit side effects to keep action */
		actctx = hash_table_lookup(result->action_contexts, act_keep.def);

		if ( actctx != NULL && actctx->seffects != NULL )
			rsef_first = actctx->seffects->first_effect;
	}

	/* Start keep action */
	if ( act_keep.def->start != NULL )
		status = act_keep.def->start
			(&act_keep, &result->action_env,  &tr_context);

	/* Execute keep action */
	if ( status == SIEVE_EXEC_OK ) {
		rsef = rsef_first;
		while ( status == SIEVE_EXEC_OK && rsef != NULL ) {
			struct sieve_side_effect *sef = &rsef->seffect;

			if ( sef->def->pre_execute != NULL )
				status = sef->def->pre_execute
					(sef, &act_keep, &result->action_env, &sef->context, tr_context);
			rsef = rsef->next;
		}

		if ( status == SIEVE_EXEC_OK && act_keep.def->execute != NULL )
			status = act_keep.def->execute
				(&act_keep, &result->action_env, tr_context);

		rsef = rsef_first;
		while ( status == SIEVE_EXEC_OK && rsef != NULL ) {
			struct sieve_side_effect *sef = &rsef->seffect;

			if ( sef->def->post_execute != NULL )
				status = sef->def->post_execute
					(sef, &act_keep, &result->action_env, tr_context);
			rsef = rsef->next;
		}
	}

	/* Finish keep action */
	if ( status == SIEVE_EXEC_OK ) {
		bool dummy = TRUE;

		if ( act_keep.def->commit != NULL )
			status = act_keep.def->commit
				(&act_keep, &result->action_env, tr_context, &dummy);

		rsef = rsef_first;
		while ( rsef != NULL ) {
			struct sieve_side_effect *sef = &rsef->seffect;
			bool keep = TRUE;

			if ( sef->def->post_commit != NULL )
				sef->def->post_commit
					(sef, &act_keep, &result->action_env, tr_context, &keep);
			rsef = rsef->next;
		}

		return status;
	}

	/* Failed, rollback */
	if ( act_keep.def->rollback != NULL )
		act_keep.def->rollback(&act_keep,
			&result->action_env, tr_context, ( status == SIEVE_EXEC_OK ));

	return status;
}

int sieve_result_implicit_keep
(struct sieve_result *result)
{
	_sieve_result_prepare_execution(result);

	return _sieve_result_implicit_keep(result, TRUE);
}

void sieve_result_mark_executed(struct sieve_result *result)
{
	struct sieve_result_action *first_action, *rac;

	first_action = ( result->last_attempted_action == NULL ?
		result->first_action : result->last_attempted_action->next );
	result->last_attempted_action = result->last_action;

	rac = first_action;
	while ( rac != NULL ) {
		if ( rac->action.def != NULL )
			rac->action.executed = TRUE;

		rac = rac->next;
	}
}


bool sieve_result_executed(struct sieve_result *result)
{
	return result->executed;
}

int sieve_result_execute
(struct sieve_result *result, bool *keep)
{
	bool implicit_keep = TRUE, executed = result->executed;
	int status = SIEVE_EXEC_OK, commit_status;
	struct sieve_result_action *rac, *first_action;
	struct sieve_result_action *last_attempted;
	int ret;

	if ( keep != NULL ) *keep = FALSE;

	/* Prepare environment */

	_sieve_result_prepare_execution(result);

	/* Make notice of this attempt */

	first_action = ( result->last_attempted_action == NULL ?
		result->first_action : result->last_attempted_action->next );
	result->last_attempted_action = result->last_action;

	/*
	 * Transaction start
	 */

	rac = first_action;
	while ( status == SIEVE_EXEC_OK && rac != NULL ) {
		struct sieve_action *act = &rac->action;

		/* Skip non-actions (inactive keep) and executed ones */
		if ( act->def == NULL || act->executed ) {
			rac = rac->next;
			continue;
		}

		if ( act->def->start != NULL ) {
			status = act->def->start
				(act, &result->action_env, &rac->tr_context);
			rac->success = ( status == SIEVE_EXEC_OK );
		}

		rac = rac->next;
	}

	/*
	 * Transaction execute
	 */

	last_attempted = rac;
	rac = first_action;
	while ( status == SIEVE_EXEC_OK && rac != NULL ) {
		struct sieve_action *act = &rac->action;
		struct sieve_result_side_effect *rsef;
		struct sieve_side_effect *sef;

		/* Skip non-actions (inactive keep) and executed ones */
		if ( act->def == NULL || act->executed ) {
			rac = rac->next;
			continue;
		}

		/* Execute pre-execute event of side effects */
		rsef = rac->seffects != NULL ? rac->seffects->first_effect : NULL;
		while ( status == SIEVE_EXEC_OK && rsef != NULL ) {
			sef = &rsef->seffect;
			if ( sef->def != NULL && sef->def->pre_execute != NULL )
				status = sef->def->pre_execute
					(sef, act, &result->action_env, &sef->context, rac->tr_context);
			rsef = rsef->next;
		}

		/* Execute the action itself */
		if ( status == SIEVE_EXEC_OK && act->def != NULL &&
			act->def->execute != NULL ) {
			status = act->def->execute
				(act, &result->action_env, rac->tr_context);
			rac->success = ( status == SIEVE_EXEC_OK );
		}

		/* Execute post-execute event of side effects */
		rsef = rac->seffects != NULL ? rac->seffects->first_effect : NULL;
		while ( status == SIEVE_EXEC_OK && rsef != NULL ) {
			sef = &rsef->seffect;
			if ( sef->def != NULL && sef->def->post_execute != NULL )
				status = sef->def->post_execute
					(sef, act, &result->action_env, rac->tr_context);
			rsef = rsef->next;
		}

		rac = rac->next;
	}

	/*
	 * Transaction commit/rollback
	 */

	commit_status = status;
	rac = first_action;
	while ( rac != NULL && rac != last_attempted ) {
		struct sieve_action *act = &rac->action;
		struct sieve_result_side_effect *rsef;
		struct sieve_side_effect *sef;

		if ( status == SIEVE_EXEC_OK ) {
			bool impl_keep = TRUE;
			int cstatus = SIEVE_EXEC_OK;

			if ( rac->keep && keep != NULL ) *keep = TRUE;

			/* Skip non-actions (inactive keep) and executed ones */
			if ( act->def == NULL || act->executed ) {
				rac = rac->next;
				continue;
			}

			if ( act->def->commit != NULL ) {
				cstatus = act->def->commit
					(act, &result->action_env, rac->tr_context, &impl_keep);
				if ( cstatus == SIEVE_EXEC_OK ) {
					act->executed = TRUE;
					result->executed = TRUE;
					executed = TRUE;
				} else {
					/* This is bad; try to salvage as much as possible */
					if (commit_status == SIEVE_EXEC_OK) {
						commit_status = cstatus;
						if (!executed) {
							/* We haven't executed anything yet; continue as rollback */
							status = cstatus;
						}
					}
					impl_keep = TRUE;
				}
			}

			if ( cstatus == SIEVE_EXEC_OK ) {
				/* Execute post_commit event of side effects */
				rsef = rac->seffects != NULL ? rac->seffects->first_effect : NULL;
				while ( rsef != NULL ) {
					sef = &rsef->seffect;
					if ( sef->def->post_commit != NULL )
						sef->def->post_commit
							(sef, act, &result->action_env, rac->tr_context, &impl_keep);
					rsef = rsef->next;
				}
			}

			implicit_keep = implicit_keep && impl_keep;
		} else {
			/* Skip non-actions (inactive keep) and executed ones */
			if ( act->def == NULL || act->executed ) {
				rac = rac->next;
				continue;
			}

			if ( act->def->rollback != NULL ) {
				act->def->rollback
					(act, &result->action_env, rac->tr_context, rac->success);
			}

			/* Rollback side effects */
			rsef = rac->seffects != NULL ? rac->seffects->first_effect : NULL;
			while ( rsef != NULL ) {
				sef = &rsef->seffect;
				if ( sef->def && sef->def->rollback != NULL )
					sef->def->rollback
						(sef, act, &result->action_env, rac->tr_context, rac->success);
				rsef = rsef->next;
			}
		}

		rac = rac->next;
	}

	if ( implicit_keep && keep != NULL ) *keep = TRUE;

	/* Return value indicates whether the caller should attempt an implicit keep
	 * of its own. So, if the above transaction fails, but the implicit keep below
	 * succeeds, the return value is still true. An error is/should be logged
	 * though.
	 */

	if ( !executed && commit_status == SIEVE_EXEC_TEMP_FAILURE )
		return commit_status;

	/* Execute implicit keep if the transaction failed or when the implicit keep
	 * was not canceled during transaction.
	 */
	if ( commit_status != SIEVE_EXEC_OK || implicit_keep ) {
		switch ((ret=_sieve_result_implicit_keep
			(result, ( commit_status != SIEVE_EXEC_OK ))) ) {
		case SIEVE_EXEC_OK:
			break;
		case SIEVE_EXEC_TEMP_FAILURE:
			if (!executed)
				return ret;
		default:
			return SIEVE_EXEC_KEEP_FAILED;
		}

		return commit_status;
	}

	/* success */
	return SIEVE_EXEC_OK;
}

/*
 * Result evaluation
 */

struct sieve_result_iterate_context {
	struct sieve_result *result;
	struct sieve_result_action *current_action;
	struct sieve_result_action *next_action;
};

struct sieve_result_iterate_context *sieve_result_iterate_init
(struct sieve_result *result)
{
	struct sieve_result_iterate_context *rictx =
		t_new(struct sieve_result_iterate_context, 1);

	rictx->result = result;
	rictx->current_action = NULL;
	rictx->next_action = result->first_action;

	return rictx;
}

const struct sieve_action *sieve_result_iterate_next
(struct sieve_result_iterate_context *rictx, bool *keep)
{
	struct sieve_result_action *rac;

	if ( rictx == NULL )
		return  NULL;

	rac = rictx->current_action = rictx->next_action;
	if ( rac != NULL ) {
		rictx->next_action = rac->next;

		if ( keep != NULL )
			*keep = rac->keep;

		return &rac->action;
	}

	return NULL;
}

void sieve_result_iterate_delete
(struct sieve_result_iterate_context *rictx)
{
	struct sieve_result *result;
	struct sieve_result_action *rac;

	if ( rictx == NULL || rictx->current_action == NULL )
		return;

	result = rictx->result;
	rac = rictx->current_action;

	/* Delete action */

	if ( rac->prev == NULL )
		result->first_action = rac->next;
	else
		rac->prev->next = rac->next;

	if ( rac->next == NULL )
		result->last_action = rac->prev;
	else
		rac->next->prev = rac->prev;

	/* Skip to next action in iteration */

	rictx->current_action = NULL;
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
}

void sieve_side_effects_list_add
(struct sieve_side_effects_list *list, const struct sieve_side_effect *seffect)
{
	struct sieve_result_side_effect *reffect;

	/* Prevent duplicates */
	reffect = list->first_effect;
	while ( reffect != NULL ) {
		if ( reffect->seffect.def == seffect->def ) return;

		reffect = reffect->next;
	}

	/* Create new side effect object */
	reffect = p_new(list->result->pool, struct sieve_result_side_effect, 1);
	reffect->seffect = *seffect;

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



