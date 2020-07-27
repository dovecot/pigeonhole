/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
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
#include "mail-storage.h"

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-script.h"
#include "sieve-error.h"
#include "sieve-interpreter.h"
#include "sieve-actions.h"
#include "sieve-message.h"

#include "sieve-result.h"

#include <stdio.h>

struct event_category event_category_sieve_action = {
	.parent = &event_category_sieve,
	.name = "sieve-action",
};

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
	struct event *event;

	/* Context data for extensions */
	ARRAY(void *) ext_contexts;

	struct sieve_action_exec_env action_env;

	struct sieve_action keep_action;
	struct sieve_action failure_action;

	unsigned int action_count;
	struct sieve_result_action *first_action;
	struct sieve_result_action *last_action;

	struct sieve_result_action *last_attempted_action;

	HASH_TABLE(const struct sieve_action_def *,
		   struct sieve_result_action_context *) action_contexts;

	bool executed:1;
	bool executed_delivery:1;
};

static const char *
sieve_result_event_log_message(struct sieve_result *result,
			       enum log_type log_type, const char *message)
{
	const struct sieve_script_env *senv =
		result->action_env.exec_env->scriptenv;

	i_assert(senv->result_amend_log_message != NULL);
	return senv->result_amend_log_message(senv, log_type, message);
}

struct sieve_result *
sieve_result_create(struct sieve_instance *svinst, pool_t pool,
		    const struct sieve_execute_env *eenv)
{
	const struct sieve_script_env *senv = eenv->scriptenv;
	const struct sieve_message_data *msgdata = eenv->msgdata;
	struct sieve_result *result;

	pool_ref(pool);

	result = p_new(pool, struct sieve_result, 1);
	result->refcount = 1;
	result->pool = pool;
	result->svinst = svinst;

	result->event = event_create(eenv->event);
	event_add_category(result->event, &event_category_sieve_action);
	if (senv->result_amend_log_message != NULL) {
		event_set_log_message_callback(
			result->event, sieve_result_event_log_message, result);
	}

	p_array_init(&result->ext_contexts, pool, 4);

	result->action_env.result = result;
	result->action_env.exec_env = eenv;
	result->action_env.event = result->event;
	result->action_env.msgctx =
		sieve_message_context_create(svinst, senv->user, msgdata);

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

static void
sieve_result_action_deinit(struct sieve_result_action *ract)
{
	event_unref(&ract->action.event);
}

void sieve_result_unref(struct sieve_result **_result)
{
	struct sieve_result *result = *_result;
	struct sieve_result_action *ract;

	i_assert(result->refcount > 0);

	if (--result->refcount != 0)
		return;

	sieve_message_context_unref(&result->action_env.msgctx);

	hash_table_destroy(&result->action_contexts);

	if (result->action_env.ehandler != NULL)
		sieve_error_handler_unref(&result->action_env.ehandler);

	ract = result->first_action;
	while (ract != NULL) {
		sieve_result_action_deinit(ract);
		ract = ract->next;
	}
	event_unref(&result->event);

	pool_unref(&result->pool);
	*_result = NULL;
}

pool_t sieve_result_pool(struct sieve_result *result)
{
	return result->pool;
}

/*
 * Getters/Setters
 */

const struct sieve_script_env *
sieve_result_get_script_env(struct sieve_result *result)
{
	return result->action_env.exec_env->scriptenv;
}

const struct sieve_message_data *
sieve_result_get_message_data(struct sieve_result *result)
{
	return result->action_env.exec_env->msgdata;
}

struct sieve_message_context *
sieve_result_get_message_context(struct sieve_result *result)
{
	return result->action_env.msgctx;
}

/*
 * Extension support
 */

void sieve_result_extension_set_context(struct sieve_result *result,
					const struct sieve_extension *ext,
					void *context)
{
	if (ext->id < 0)
		return;

	array_idx_set(&result->ext_contexts, (unsigned int) ext->id, &context);
}

const void *
sieve_result_extension_get_context(struct sieve_result *result,
				   const struct sieve_extension *ext)
{
	void * const *ctx;

	if (ext->id < 0 || ext->id >= (int) array_count(&result->ext_contexts))
		return NULL;

	ctx = array_idx(&result->ext_contexts, (unsigned int) ext->id);

	return *ctx;
}

/*
 * Result composition
 */

static void
sieve_result_init_action_event(struct sieve_result *result,
			       struct sieve_action *action, bool add_prefix)
{
	const char *name = sieve_action_name(action);

	if (action->event != NULL)
		return;

	action->event = event_create(result->event);
	if (add_prefix && name != NULL) {
		event_set_append_log_prefix(
			action->event, t_strconcat(name, " action: ", NULL));
	}
	event_add_str(action->event, "action_name", name);
	event_add_str(action->event, "script_location", action->location);
}

void sieve_result_add_implicit_side_effect(
	struct sieve_result *result, const struct sieve_action_def *to_action,
	bool to_keep, const struct sieve_extension *ext,
	const struct sieve_side_effect_def *seff_def, void *context)
{
	struct sieve_result_action_context *actctx = NULL;
	struct sieve_side_effect seffect;

	to_action = to_keep ? &act_store : to_action;

	if (!hash_table_is_created(result->action_contexts)) {
		hash_table_create_direct(&result->action_contexts,
					 result->pool, 0);
	} else {
		actctx = hash_table_lookup(result->action_contexts, to_action);
	}

	if (actctx == NULL) {
		actctx = p_new(result->pool,
			       struct sieve_result_action_context, 1);
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

static int
sieve_result_side_effects_merge(const struct sieve_runtime_env *renv,
				const struct sieve_action *action,
				struct sieve_result_action *old_action,
				struct sieve_side_effects_list *new_seffects)
{
	struct sieve_side_effects_list *old_seffects = old_action->seffects;
	int ret;
	struct sieve_result_side_effect *rsef, *nrsef;

	/* Allow side-effects to merge with existing copy */

	/* Merge existing side effects */
	rsef = old_seffects != NULL ? old_seffects->first_effect : NULL;
	while (rsef != NULL) {
		struct sieve_side_effect *seffect = &rsef->seffect;
		bool found = FALSE;

		if (seffect->def != NULL && seffect->def->merge != NULL) {
			/* Try to find it among the new */
			nrsef = (new_seffects != NULL ?
				 new_seffects->first_effect : NULL);
			while (nrsef != NULL) {
				struct sieve_side_effect *nseffect = &nrsef->seffect;

				if (nseffect->def == seffect->def) {
					if (seffect->def->merge(
						renv, action, seffect, nseffect,
						&seffect->context) < 0)
						return -1;

					found = TRUE;
					break;
				}
				nrsef = nrsef->next;
			}

			/* Not found? */
			if (!found && seffect->def->merge(
				renv, action, seffect, NULL,
				&rsef->seffect.context) < 0)
				return -1;
		}
		rsef = rsef->next;
	}

	/* Merge new Side effects */
	nrsef = new_seffects != NULL ? new_seffects->first_effect : NULL;
	while (nrsef != NULL) {
		struct sieve_side_effect *nseffect = &nrsef->seffect;
		bool found = FALSE;

		if (nseffect->def != NULL && nseffect->def->merge != NULL) {
			/* Try to find it among the exising */
			rsef = (old_seffects != NULL ?
				old_seffects->first_effect : NULL);
			while (rsef != NULL) {
				if (rsef->seffect.def == nseffect->def) {
					found = TRUE;
					break;
				}
				rsef = rsef->next;
			}

			/* Not found? */
			if (!found) {
				void *new_context = NULL;

				if ((ret = nseffect->def->merge(
					renv, action, nseffect, nseffect,
					&new_context)) < 0)
					return -1;

				if (ret != 0) {
					if (old_action->seffects == NULL) {
						old_action->seffects = old_seffects =
							sieve_side_effects_list_create(renv->result);
					}

					nseffect->context = new_context;

					/* Add side effect */
					sieve_side_effects_list_add(old_seffects,
								    nseffect);
				}
			}
		}
		nrsef = nrsef->next;
	}

	return 1;
}

static void
sieve_result_action_detach(struct sieve_result *result,
			   struct sieve_result_action *raction)
{
	if (result->first_action == raction)
		result->first_action = raction->next;

	if (result->last_action == raction)
		result->last_action = raction->prev;

	if (result->last_attempted_action == raction)
		result->last_attempted_action = raction->prev;

	if (raction->next != NULL)
		raction->next->prev = raction->prev;
	if (raction->prev != NULL)
		raction->prev->next = raction->next;

	raction->next = NULL;
	raction->prev = NULL;

	if (result->action_count > 0)
		result->action_count--;
}

static int
_sieve_result_add_action(const struct sieve_runtime_env *renv,
			 const struct sieve_extension *ext, const char *name,
			 const struct sieve_action_def *act_def,
			 struct sieve_side_effects_list *seffects,
			 void *context, unsigned int instance_limit,
			 bool preserve_mail, bool keep)
{
	int ret = 0;
	unsigned int instance_count = 0;
	struct sieve_instance *svinst = renv->exec_env->svinst;
	struct sieve_result *result = renv->result;
	struct sieve_result_action *raction = NULL, *kaction = NULL;
	struct sieve_action action;

	i_assert(name != NULL || act_def != NULL);
	action.name = name;
	action.def = act_def;
	action.ext = ext;
	action.location = sieve_runtime_get_full_command_location(renv);
	action.context = context;
	action.executed = FALSE;

	/* First, check for duplicates or conflicts */
	raction = result->first_action;
	while (raction != NULL) {
		const struct sieve_action *oact = &raction->action;

		if (keep && raction->keep) {
			/* Duplicate keep */
			if (raction->action.def == NULL ||
			    raction->action.executed) {
				/* Keep action from preceeding execution */

				/* Detach existing keep action */
				sieve_result_action_detach(result, raction);

				/* Merge existing side-effects with new keep action */
				if (kaction == NULL)
					kaction = raction;

				if ((ret = sieve_result_side_effects_merge(
					renv, &action, kaction, seffects)) <= 0)
					return ret;
			} else {
				/* True duplicate */
				return sieve_result_side_effects_merge(
					renv, &action, raction, seffects);
			}
		} else if ( act_def != NULL && raction->action.def == act_def ) {
			instance_count++;

			/* Possible duplicate */
			if (act_def->check_duplicate != NULL) {
				if ((ret = act_def->check_duplicate(
					renv, &action, &raction->action)) < 0)
					return ret;

				/* Duplicate */
				if (ret == 1) {
					if (keep && !raction->keep) {
						/* New keep has higher precedence than
						   existing duplicate non-keep action.
						   So, take over the result action object
						   and transform it into a keep.
						 */
						if ((ret = sieve_result_side_effects_merge(
							renv, &action, raction, seffects)) < 0)
							return ret;

						if (kaction == NULL) {
							raction->action.context = NULL;
							raction->action.location =
								p_strdup(result->pool, action.location);

							/* Note that existing execution
							   status is retained, making sure
							   that keep is not executed
							   multiple times.
							 */
							kaction = raction;
						} else {
							sieve_result_action_detach(result, raction);

							if ((ret = sieve_result_side_effects_merge(
								renv, &action, kaction,
								raction->seffects)) < 0)
								return ret;
						}
					} else {
						/* Merge side-effects, but don't add new action
						 */
						return sieve_result_side_effects_merge(
							renv, &action, raction, seffects);
					}
				}
			}
		} else {
			if (act_def != NULL && oact->def != NULL) {
				/* Check conflict */
				if (act_def->check_conflict != NULL &&
				    (ret = act_def->check_conflict(
					renv, &action, &raction->action)) != 0)
					return ret;

				if (!raction->action.executed &&
				    oact->def->check_conflict != NULL &&
				    (ret = oact->def->check_conflict(
					renv, &raction->action, &action)) != 0)
					return ret;
			}
		}
		raction = raction->next;
	}

	if (kaction != NULL) {
		/* Use existing keep action to define new one */
		raction = kaction;
	} else {
		/* Check policy limit on total number of actions */
		if (svinst->max_actions > 0 &&
		    result->action_count >= svinst->max_actions)
		{
			sieve_runtime_error(
				renv, action.location,
				"total number of actions exceeds policy limit "
				"(%u > %u)",
				result->action_count+1, svinst->max_actions);
			return -1;
		}

		/* Check policy limit on number of this class of actions */
		if (instance_limit > 0 && instance_count >= instance_limit) {
			sieve_runtime_error(
				renv, action.location,
				"number of %s actions exceeds policy limit "
				"(%u > %u)",
				act_def->name, instance_count+1,
				instance_limit);
			return -1;
		}

		/* Create new action object */
		raction = p_new(result->pool, struct sieve_result_action, 1);
		raction->action.executed = FALSE;
		raction->seffects = seffects;
		raction->tr_context = NULL;
		raction->success = FALSE;
	}

	raction->action.name = (action.name == NULL ?
				act_def->name :
				p_strdup(result->pool, action.name));
	raction->action.context = context;
	raction->action.def = act_def;
	raction->action.ext = ext;
	raction->action.location = p_strdup(result->pool, action.location);
	raction->keep = keep;

	if (raction->prev == NULL && raction != result->first_action) {
		/* Add */
		if (result->first_action == NULL) {
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
		if (hash_table_is_created(result->action_contexts)) {
			struct sieve_result_action_context *actctx;

			/* Check for implicit side effects to this particular
			   action */
			actctx = hash_table_lookup(
				result->action_contexts,
				(keep ? &act_store : act_def));

			if (actctx != NULL) {
				struct sieve_result_side_effect *iseff;

				/* Iterate through all implicit side effects and
				   add those that are missing.
				 */
				iseff = actctx->seffects->first_effect;
				while (iseff != NULL) {
					struct sieve_result_side_effect *seff;
					bool exists = FALSE;

					/* Scan for presence */
					if (seffects != NULL) {
						seff = seffects->first_effect;
						while (seff != NULL) {
							if (seff->seffect.def == iseff->seffect.def) {
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
					if (!exists) {
						sieve_side_effects_list_add(seffects, &iseff->seffect);
					}

					iseff = iseff->next;
				}
			}
		}
	}

	if (preserve_mail) {
		raction->action.mail = sieve_message_get_mail(renv->msgctx);
		sieve_message_snapshot(renv->msgctx);
	} else {
		raction->action.mail = NULL;
	}

	sieve_result_init_action_event(result, &raction->action, !keep);
	return 0;
}

int sieve_result_add_action(const struct sieve_runtime_env *renv,
			    const struct sieve_extension *ext, const char *name,
			    const struct sieve_action_def *act_def,
			    struct sieve_side_effects_list *seffects,
			    void *context, unsigned int instance_limit,
			    bool preserve_mail)
{
	return _sieve_result_add_action(renv, ext, name, act_def, seffects,
					context, instance_limit, preserve_mail,
					FALSE);
}

int sieve_result_add_keep(const struct sieve_runtime_env *renv,
			  struct sieve_side_effects_list *seffects)
{
	return _sieve_result_add_action(renv, renv->result->keep_action.ext,
					"keep", renv->result->keep_action.def,
					seffects, NULL, 0, TRUE, TRUE);
}

void sieve_result_set_keep_action(struct sieve_result *result,
				  const struct sieve_extension *ext,
				  const struct sieve_action_def *act_def)
{
	result->keep_action.def = act_def;
	result->keep_action.ext = ext;
}

void sieve_result_set_failure_action(struct sieve_result *result,
				     const struct sieve_extension *ext,
				     const struct sieve_action_def *act_def)
{
	result->failure_action.def = act_def;
	result->failure_action.ext = ext;
}

/*
 * Result printing
 */

void sieve_result_vprintf(const struct sieve_result_print_env *penv,
			  const char *fmt, va_list args)
{
	string_t *outbuf = t_str_new(128);

	str_vprintfa(outbuf, fmt, args);

	o_stream_nsend(penv->stream, str_data(outbuf), str_len(outbuf));
}

void sieve_result_printf(const struct sieve_result_print_env *penv,
			 const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	sieve_result_vprintf(penv, fmt, args);
	va_end(args);
}

void sieve_result_action_printf(const struct sieve_result_print_env *penv,
				const char *fmt, ...)
{
	string_t *outbuf = t_str_new(128);
	va_list args;

	va_start(args, fmt);
	str_append(outbuf, " * ");
	str_vprintfa(outbuf, fmt, args);
	str_append_c(outbuf, '\n');
	va_end(args);

	o_stream_nsend(penv->stream, str_data(outbuf), str_len(outbuf));
}

void sieve_result_seffect_printf(const struct sieve_result_print_env *penv,
				 const char *fmt, ...)
{
	string_t *outbuf = t_str_new(128);
	va_list args;

	va_start(args, fmt);
	str_append(outbuf, "        + ");
	str_vprintfa(outbuf, fmt, args);
	str_append_c(outbuf, '\n');
	va_end(args);

	o_stream_nsend(penv->stream, str_data(outbuf), str_len(outbuf));
}

static void
sieve_result_print_side_effects(struct sieve_result_print_env *rpenv,
				const struct sieve_action *action,
				struct sieve_side_effects_list *slist,
				bool *implicit_keep)
{
	struct sieve_result_side_effect *rsef;

	/* Print side effects */
	rsef = (slist != NULL ? slist->first_effect : NULL);
	while (rsef != NULL) {
		if (rsef->seffect.def != NULL) {
			const struct sieve_side_effect *sef = &rsef->seffect;

			if (sef->def->print != NULL) {
				sef->def->print(sef, action, rpenv,
						implicit_keep);
			}
		}
		rsef = rsef->next;
	}
}

static void
sieve_result_print_implicit_side_effects(struct sieve_result_print_env *rpenv)
{
	struct sieve_result *result = rpenv->result;
	bool dummy = TRUE;

	/* Print any implicit side effects if applicable */
	if (hash_table_is_created(result->action_contexts)) {
		struct sieve_result_action_context *actctx;

		/* Check for implicit side effects to keep action */
		actctx = hash_table_lookup(rpenv->result->action_contexts,
					   &act_store);

		if (actctx != NULL && actctx->seffects != NULL) {
			sieve_result_print_side_effects(
				rpenv, &result->keep_action,
				actctx->seffects, &dummy);
		}
	}
}

bool sieve_result_print(struct sieve_result *result,
			const struct sieve_script_env *senv,
			struct ostream *stream, bool *keep)
{
	struct sieve_action act_keep = result->keep_action;
	struct sieve_result_print_env penv;
	bool implicit_keep = TRUE;
	struct sieve_result_action *rac, *first_action;

	first_action = (result->last_attempted_action == NULL ?
			result->first_action :
			result->last_attempted_action->next);

	if (keep != NULL)
		*keep = FALSE;

	/* Prepare environment */

	penv.result = result;
	penv.stream = stream;
	penv.scriptenv = senv;

	sieve_result_printf(&penv, "\nPerformed actions:\n\n");

	if (first_action == NULL) {
		sieve_result_printf(&penv, "  (none)\n");
	} else {
		rac = first_action;
		while (rac != NULL) {
			bool impl_keep = TRUE;
			const struct sieve_action *act = &rac->action;

			if (rac->keep && keep != NULL)
				*keep = TRUE;

			if (act->def != NULL) {
				if (act->def->print != NULL)
					act->def->print(act, &penv, &impl_keep);
				else {
					sieve_result_action_printf(
						&penv, "%s", act->def->name);
				}
			} else {
				if (rac->keep) {
					sieve_result_action_printf(&penv, "keep");
					impl_keep = FALSE;
				} else {
					sieve_result_action_printf(&penv, "[NULL]");
				}
			}

			/* Print side effects */
			sieve_result_print_side_effects(
				&penv, &rac->action, rac->seffects, &impl_keep);

			implicit_keep = implicit_keep && impl_keep;

			rac = rac->next;
		}
	}

	if (implicit_keep && keep != NULL)
		*keep = TRUE;

	sieve_result_printf(&penv, "\nImplicit keep:\n\n");

	if (implicit_keep) {
		bool dummy = TRUE;

		if (act_keep.def == NULL) {
			sieve_result_action_printf(&penv, "keep");

			sieve_result_print_implicit_side_effects(&penv);
		} else {
			/* Scan for execution of keep-equal actions */
			rac = result->first_action;
			while (act_keep.def != NULL && rac != NULL) {
				if (rac->action.def == act_keep.def &&
				    act_keep.def->equals != NULL &&
				    act_keep.def->equals(senv, NULL, &rac->action) &&
				    rac->action.executed)
					act_keep.def = NULL;

				rac = rac->next;
			}

			if (act_keep.def == NULL) {
				sieve_result_printf(&penv,
					"  (none; keep or equivalent action executed earlier)\n");
			} else {
				act_keep.def->print(&act_keep, &penv, &dummy);

				sieve_result_print_implicit_side_effects(&penv);
			}
		}
	} else {
		sieve_result_printf(&penv, "  (none)\n");
	}

	sieve_result_printf(&penv, "\n");

	return TRUE;
}

/*
 * Result execution
 */

static void
sieve_result_prepare_action_env(struct sieve_result *result,
				const struct sieve_action *act) ATTR_NULL(2)
{
	result->action_env.action = act;
	result->action_env.event = act->event;
}

static void
sieve_result_finish_action_env(struct sieve_result *result)
{
	result->action_env.action = NULL;
	result->action_env.event = result->event;
}

static void
_sieve_result_prepare_execution(struct sieve_result *result,
				struct sieve_error_handler *ehandler)
{
	result->action_env.event = result->event;
	result->action_env.ehandler = ehandler;
}

static int
_sieve_result_implicit_keep(struct sieve_result *result, bool rollback)
{
	const struct sieve_action_exec_env *aenv = &result->action_env;
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct sieve_result_action *rac, *kac;
	int status = SIEVE_EXEC_OK;
	struct sieve_result_side_effect *rsef, *rsef_first = NULL;
	void *tr_context = NULL;
	struct sieve_action act_keep;

	if ((eenv->flags & SIEVE_EXECUTE_FLAG_DEFER_KEEP) != 0)
		return SIEVE_EXEC_OK;

	if (rollback)
		act_keep = result->failure_action;
	else
		act_keep = result->keep_action;
	act_keep.name = "keep";
	act_keep.mail = NULL;

	/* If keep is a non-action, return right away */
	if (act_keep.def == NULL)
		return SIEVE_EXEC_OK;

	/* Scan for deferred keep */
	kac = result->last_action;
	while (kac != NULL && kac->action.executed) {
		if (kac->keep && kac->action.def == NULL)
			break;
		kac = kac->prev;
	}

	if (kac == NULL) {
		if (!rollback)
			act_keep.mail = sieve_message_get_mail(aenv->msgctx);

		/* Scan for execution of keep-equal actions */
		rac = result->first_action;
		while (rac != NULL) {
			if (rac->action.def == act_keep.def &&
			    act_keep.def->equals != NULL &&
			    act_keep.def->equals(eenv->scriptenv, NULL,
						 &rac->action) &&
			    rac->action.executed)
				return SIEVE_EXEC_OK;

			rac = rac->next;
		}
	} else if (!rollback) {
		act_keep.location = kac->action.location;
		act_keep.mail = kac->action.mail;
		if (kac->seffects != NULL)
			rsef_first = kac->seffects->first_effect;
	}

	if (rsef_first == NULL) {
		/* Apply any implicit side effects if applicable */
		if (!rollback &&
		    hash_table_is_created(result->action_contexts)) {
			struct sieve_result_action_context *actctx;

			/* Check for implicit side effects to keep action */
			actctx = hash_table_lookup(result->action_contexts,
						   act_keep.def);

			if (actctx != NULL && actctx->seffects != NULL)
				rsef_first = actctx->seffects->first_effect;
		}
	}

	/* Initialize keep action event */
	sieve_result_init_action_event(result, &act_keep, FALSE);

	/* Start keep action */
	if (act_keep.def->start != NULL) {
		sieve_result_prepare_action_env(result, &act_keep);
		status = act_keep.def->start(aenv, &tr_context);
	}

	/* Execute keep action */
	if (status == SIEVE_EXEC_OK) {
		rsef = rsef_first;
		while (status == SIEVE_EXEC_OK && rsef != NULL) {
			struct sieve_side_effect *sef = &rsef->seffect;

			if (sef->def->pre_execute != NULL) {
				sieve_result_prepare_action_env(result,
								&act_keep);
				status = sef->def->pre_execute(sef, aenv,
							       &sef->context,
							       tr_context);
			}
			rsef = rsef->next;
		}

		if (status == SIEVE_EXEC_OK && act_keep.def->execute != NULL) {
			sieve_result_prepare_action_env(result, &act_keep);
			status = act_keep.def->execute(aenv, tr_context);
		}

		rsef = rsef_first;
		while (status == SIEVE_EXEC_OK && rsef != NULL) {
			struct sieve_side_effect *sef = &rsef->seffect;

			if (sef->def->post_execute != NULL) {
				sieve_result_prepare_action_env(result,
								&act_keep);
				status = sef->def->post_execute(sef, aenv,
								tr_context);
			}
			rsef = rsef->next;
		}
	}

	/* Commit keep action */
	if (status == SIEVE_EXEC_OK) {
		bool dummy = TRUE;

		if (act_keep.def->commit != NULL) {
			sieve_result_prepare_action_env(result, &act_keep);
			status = act_keep.def->commit(aenv, tr_context, &dummy);
		}

		rsef = rsef_first;
		while (rsef != NULL) {
			struct sieve_side_effect *sef = &rsef->seffect;
			bool keep = TRUE;

			if (sef->def->post_commit != NULL) {
				sieve_result_prepare_action_env(result,
								&act_keep);
				sef->def->post_commit(sef, aenv, tr_context,
						      &keep);
			}
			rsef = rsef->next;
		}
	} else {
		/* Failed, rollback */
		if (act_keep.def->rollback != NULL) {
			sieve_result_prepare_action_env(result, &act_keep);
			act_keep.def->rollback(aenv, tr_context,
					       (status == SIEVE_EXEC_OK));
		}
	}

	/* Finish keep action */
	if (act_keep.def->finish != NULL) {
		sieve_result_prepare_action_env(result, &act_keep);
		act_keep.def->finish(aenv, TRUE, tr_context, status);
	}

	sieve_result_finish_action_env(result);
	event_unref(&act_keep.event);

	if (status == SIEVE_EXEC_FAILURE)
		status = SIEVE_EXEC_KEEP_FAILED;
	return status;
}

int sieve_result_implicit_keep(struct sieve_result *result,
			       struct sieve_error_handler *ehandler,
			       bool success)
{
	int ret;

	_sieve_result_prepare_execution(result, ehandler);

	ret = _sieve_result_implicit_keep(result, !success);

	result->action_env.ehandler = NULL;

	return ret;
}

void sieve_result_mark_executed(struct sieve_result *result)
{
	struct sieve_result_action *first_action, *rac;

	first_action = (result->last_attempted_action == NULL ?
			result->first_action :
			result->last_attempted_action->next);
	result->last_attempted_action = result->last_action;

	rac = first_action;
	while (rac != NULL) {
		if (rac->action.def != NULL)
			rac->action.executed = TRUE;

		rac = rac->next;
	}
}


bool sieve_result_executed(struct sieve_result *result)
{
	return result->executed;
}

bool sieve_result_executed_delivery(struct sieve_result *result)
{
	return result->executed_delivery;
}

static int
sieve_result_transaction_start(struct sieve_result *result,
			       struct sieve_result_action *first,
			       struct sieve_result_action **last_r)
{
	const struct sieve_execute_env *eenv = result->action_env.exec_env;
	const struct sieve_script_env *senv = eenv->scriptenv;
	struct sieve_result_action *rac = first;
	int status = SIEVE_EXEC_OK;
	bool dup_flushed = FALSE;

	while (status == SIEVE_EXEC_OK && rac != NULL) {
		struct sieve_action *act = &rac->action;

		/* Skip non-actions (inactive keep) and executed ones */
		if (act->def == NULL || act->executed) {
			rac = rac->next;
			continue;
		}

		if ((act->def->flags & SIEVE_ACTFLAG_MAIL_STORAGE) != 0 &&
		    !dup_flushed) {
			sieve_action_duplicate_flush(senv);
			dup_flushed = TRUE;
		}

		if (act->def->start != NULL) {
			sieve_result_prepare_action_env(result, act);
			status = act->def->start(&result->action_env,
						 &rac->tr_context);
			rac->success = (status == SIEVE_EXEC_OK);
		}
		rac = rac->next;
	}
	sieve_result_finish_action_env(result);

	*last_r = rac;
	return status;
}

static int
sieve_result_transaction_execute(struct sieve_result *result,
				 struct sieve_result_action *first)
{
	struct sieve_result_action *rac = first;
	int status = SIEVE_EXEC_OK;

	while (status == SIEVE_EXEC_OK && rac != NULL) {
		struct sieve_action *act = &rac->action;
		struct sieve_result_side_effect *rsef;
		struct sieve_side_effect *sef;

		/* Skip non-actions (inactive keep) and executed ones */
		if (act->def == NULL || act->executed) {
			rac = rac->next;
			continue;
		}


		/* Execute pre-execute event of side effects */
		rsef = (rac->seffects != NULL ?
			rac->seffects->first_effect : NULL);
		while (status == SIEVE_EXEC_OK && rsef != NULL) {
			sef = &rsef->seffect;
			if (sef->def != NULL && sef->def->pre_execute != NULL) {
				sieve_result_prepare_action_env(result, act);
				status = sef->def->pre_execute(
					sef, &result->action_env,
					&sef->context, rac->tr_context);
			}
			rsef = rsef->next;
		}

		/* Execute the action itself */
		if (status == SIEVE_EXEC_OK && act->def != NULL &&
		    act->def->execute != NULL) {
			sieve_result_prepare_action_env(result, act);
			status = act->def->execute(&result->action_env,
						   rac->tr_context);
		}

		/* Execute post-execute event of side effects */
		rsef = (rac->seffects != NULL ?
			rac->seffects->first_effect : NULL);
		while (status == SIEVE_EXEC_OK && rsef != NULL) {
			sef = &rsef->seffect;
			if (sef->def != NULL &&
			    sef->def->post_execute != NULL) {
				sieve_result_prepare_action_env(result, act);
				status = sef->def->post_execute(
					sef, &result->action_env,
					rac->tr_context);
			}
			rsef = rsef->next;
		}

		rac->success = (status == SIEVE_EXEC_OK);
		rac = rac->next;
	}
	sieve_result_finish_action_env(result);

	return status;
}

static int
sieve_result_action_commit(struct sieve_result *result,
			   struct sieve_result_action *rac, bool *impl_keep)
{
	struct sieve_action *act = &rac->action;
	struct sieve_result_side_effect *rsef;
	int cstatus = SIEVE_EXEC_OK;

	if (act->def->commit != NULL) {
		sieve_result_prepare_action_env(result, act);
		cstatus = act->def->commit(&result->action_env,
					   rac->tr_context, impl_keep);
		if (cstatus == SIEVE_EXEC_OK) {
			act->executed = TRUE;
			result->executed = TRUE;
		}
	}

	if (cstatus == SIEVE_EXEC_OK) {
		/* Execute post_commit event of side effects */
		rsef = (rac->seffects != NULL ?
			rac->seffects->first_effect : NULL);
		while (rsef != NULL) {
			struct sieve_side_effect *sef = &rsef->seffect;

			if (sef->def->post_commit != NULL) {
				sieve_result_prepare_action_env(result, act);
				sef->def->post_commit(
					sef, &result->action_env,
					rac->tr_context, impl_keep);
			}
			rsef = rsef->next;
		}
	}
	sieve_result_finish_action_env(result);

	return cstatus;
}

static void
sieve_result_action_rollback(struct sieve_result *result,
			     struct sieve_result_action *rac)
{
	struct sieve_action *act = &rac->action;
	struct sieve_result_side_effect *rsef;

	if (act->def->rollback != NULL) {
		sieve_result_prepare_action_env(result, act);
		act->def->rollback(&result->action_env, rac->tr_context,
				   rac->success);
	}

	/* Rollback side effects */
	rsef = (rac->seffects != NULL ? rac->seffects->first_effect : NULL);
	while (rsef != NULL) {
		struct sieve_side_effect *sef = &rsef->seffect;

		if (sef->def != NULL && sef->def->rollback != NULL) {
			sieve_result_prepare_action_env(result, act);
			sef->def->rollback(sef, &result->action_env,
					   rac->tr_context, rac->success);
		}
		rsef = rsef->next;
	}
	sieve_result_finish_action_env(result);
}

static int
sieve_result_action_commit_or_rollback(struct sieve_result *result,
				       struct sieve_result_action *rac,
				       int status, bool *implicit_keep,
				       bool *keep, int *commit_status)
{
	struct sieve_action *act = &rac->action;

	if (status == SIEVE_EXEC_OK) {
		bool impl_keep = TRUE;
		int cstatus = SIEVE_EXEC_OK;

		if (rac->keep && keep != NULL)
			*keep = TRUE;

		/* Skip non-actions (inactive keep) and executed ones */
		if (act->def == NULL || act->executed)
			return status;

		cstatus = sieve_result_action_commit(result, rac, &impl_keep);
		if (cstatus != SIEVE_EXEC_OK) {
			/* This is bad; try to salvage as much as possible */
			if (*commit_status == SIEVE_EXEC_OK) {
				*commit_status = cstatus;
				if (!result->executed) {
					/* We haven't executed anything yet;
					   continue as rollback */
					status = cstatus;
				}
			}
			impl_keep = TRUE;
		}

		*implicit_keep = *implicit_keep && impl_keep;
	} else {
		/* Skip non-actions (inactive keep) and executed ones */
		if (act->def == NULL || act->executed)
			return status;

		sieve_result_action_rollback(result, rac);
	}

	if (rac->keep) {
		if (status == SIEVE_EXEC_FAILURE)
			status = SIEVE_EXEC_KEEP_FAILED;
		if (*commit_status == SIEVE_EXEC_FAILURE)
			*commit_status = SIEVE_EXEC_KEEP_FAILED;
	}

	return status;
}

static int
sieve_result_transaction_commit_or_rollback(struct sieve_result *result,
					    int status,
					    struct sieve_result_action *first,
					    struct sieve_result_action *last,
					    bool *implicit_keep, bool *keep)
{
	struct sieve_result_action *rac;
	int commit_status = status;
	bool seen_delivery = FALSE;

	/* First commit/rollback all storage actions */
	rac = first;
	while (rac != NULL && rac != last) {
		struct sieve_action *act = &rac->action;

		if (act->def == NULL ||
		    (act->def->flags & SIEVE_ACTFLAG_MAIL_STORAGE) == 0) {
			rac = rac->next;
			continue;
		}

		status = sieve_result_action_commit_or_rollback(
			result, rac, status, implicit_keep, keep,
			&commit_status);

		if (act->def != NULL &&
		    (act->def->flags & SIEVE_ACTFLAG_TRIES_DELIVER) != 0)
			seen_delivery = TRUE;

		rac = rac->next;
	}

	/* Then commit/rollback all other actions */
	rac = first;
	while (rac != NULL && rac != last) {
		struct sieve_action *act = &rac->action;

		if (act->def != NULL &&
		    (act->def->flags & SIEVE_ACTFLAG_MAIL_STORAGE) != 0) {
			rac = rac->next;
			continue;
		}

		status = sieve_result_action_commit_or_rollback(
			result, rac, status, implicit_keep, keep,
			&commit_status);

		if (act->def != NULL &&
		    (act->def->flags & SIEVE_ACTFLAG_TRIES_DELIVER) != 0)
			seen_delivery = TRUE;

		rac = rac->next;
	}

	if (*implicit_keep && keep != NULL) *keep = TRUE;

	if (commit_status == SIEVE_EXEC_OK) {
		result->executed_delivery =
			result->executed_delivery || seen_delivery;
	}

	return commit_status;
}

static void
sieve_result_transaction_finish(struct sieve_result *result, bool last,
				int status)
{
	struct sieve_result_action *rac = result->first_action;

	while (rac != NULL) {
		struct sieve_action *act = &rac->action;

		/* Skip non-actions (inactive keep) and executed ones */
		if (act->def == NULL || act->executed) {
			rac = rac->next;
			continue;
		}

		if (act->def->finish != NULL) {
			sieve_result_prepare_action_env(result, act);
			act->def->finish(&result->action_env, last,
					 rac->tr_context, status);
		}

		rac = rac->next;
	}
	sieve_result_finish_action_env(result);
}

int sieve_result_execute(struct sieve_result *result, bool last, bool *keep,
			 struct sieve_error_handler *ehandler)
{
	int status = SIEVE_EXEC_OK, result_status;
	struct sieve_result_action *first_action, *last_action;
	bool implicit_keep = TRUE;
	int ret;

	if (keep != NULL)
		*keep = FALSE;

	/* Prepare environment */

	_sieve_result_prepare_execution(result, ehandler);

	/* Make notice of this attempt */

	first_action = (result->last_attempted_action == NULL ?
			result->first_action :
			result->last_attempted_action->next);
	result->last_attempted_action = result->last_action;

	/* Transaction start */

	status = sieve_result_transaction_start(result, first_action,
						&last_action);

	/* Transaction execute */

	if (status == SIEVE_EXEC_OK) {
		status = sieve_result_transaction_execute(result, first_action);
	}

	/* Transaction commit/rollback */

	status = sieve_result_transaction_commit_or_rollback(
		result, status, first_action, last_action,
		&implicit_keep, keep);

	/* Perform implicit keep if necessary */

	result_status = status;
	if (result->executed || status != SIEVE_EXEC_TEMP_FAILURE) {
		/* Execute implicit keep if the transaction failed or when the implicit
		 * keep was not canceled during transaction.
		 */
		if (status != SIEVE_EXEC_OK || implicit_keep) {
			switch ((ret = _sieve_result_implicit_keep(
				result, (status != SIEVE_EXEC_OK)))) {
			case SIEVE_EXEC_OK:
				if (result_status == SIEVE_EXEC_TEMP_FAILURE)
					result_status = SIEVE_EXEC_FAILURE;
				break;
			case SIEVE_EXEC_TEMP_FAILURE:
				if (!result->executed) {
					result_status = ret;
					break;
				}
				/* fall through */
			default:
				result_status = SIEVE_EXEC_KEEP_FAILED;
			}
		}
		if (status == SIEVE_EXEC_OK)
			status = result_status;
	}

	/* Finish execution */

	sieve_result_transaction_finish(result, last, status);

	result->action_env.ehandler = NULL;
	return result_status;
}

void sieve_result_finish(struct sieve_result *result,
			 struct sieve_error_handler *ehandler, bool success)
{
	int status = (success ? SIEVE_EXEC_OK : SIEVE_EXEC_FAILURE);

	/* Prepare environment */

	_sieve_result_prepare_execution(result, ehandler);

	/* Finish execution */

	sieve_result_transaction_finish(result, TRUE, status);

	result->action_env.ehandler = NULL;
}

/*
 * Result evaluation
 */

struct sieve_result_iterate_context {
	struct sieve_result *result;
	struct sieve_result_action *current_action;
	struct sieve_result_action *next_action;
};

struct sieve_result_iterate_context *
sieve_result_iterate_init(struct sieve_result *result)
{
	struct sieve_result_iterate_context *rictx =
		t_new(struct sieve_result_iterate_context, 1);

	rictx->result = result;
	rictx->current_action = NULL;
	rictx->next_action = result->first_action;

	return rictx;
}

const struct sieve_action *
sieve_result_iterate_next(struct sieve_result_iterate_context *rictx,
			  bool *keep)
{
	struct sieve_result_action *rac;

	if (rictx == NULL)
		return  NULL;

	rac = rictx->current_action = rictx->next_action;
	if (rac != NULL) {
		rictx->next_action = rac->next;

		if (keep != NULL)
			*keep = rac->keep;

		return &rac->action;
	}

	return NULL;
}

void sieve_result_iterate_delete(struct sieve_result_iterate_context *rictx)
{
	struct sieve_result *result;
	struct sieve_result_action *rac;

	if (rictx == NULL || rictx->current_action == NULL)
		return;

	result = rictx->result;
	rac = rictx->current_action;

	/* Delete action */

	if (rac->prev == NULL)
		result->first_action = rac->next;
	else
		rac->prev->next = rac->next;

	if (rac->next == NULL)
		result->last_action = rac->prev;
	else
		rac->next->prev = rac->prev;

	sieve_result_action_deinit(rac);

	/* Skip to next action in iteration */

	rictx->current_action = NULL;
}

/*
 * Side effects list
 */

struct sieve_side_effects_list *
sieve_side_effects_list_create(struct sieve_result *result)
{
	struct sieve_side_effects_list *list =
		p_new(result->pool, struct sieve_side_effects_list, 1);

	list->result = result;
	list->first_effect = NULL;
	list->last_effect = NULL;

	return list;
}

void sieve_side_effects_list_add(struct sieve_side_effects_list *list,
				 const struct sieve_side_effect *seffect)
{
	struct sieve_result_side_effect *reffect, *reffect_pos;

	/* Prevent duplicates */
	reffect = list->first_effect;
	reffect_pos = NULL;
	while (reffect != NULL) {
		const struct sieve_side_effect_def *ref_def = reffect->seffect.def;
		const struct sieve_side_effect_def *sef_def = seffect->def;

		if (sef_def == ref_def) {
			/* already listed */
			i_assert(reffect_pos == NULL);
			return;
		}
		if (sef_def->precedence > ref_def->precedence) {
			/* insert it before this position */
			reffect_pos = reffect;
		}

		reffect = reffect->next;
	}

	/* Create new side effect object */
	reffect = p_new(list->result->pool, struct sieve_result_side_effect, 1);
	reffect->seffect = *seffect;

	if (reffect_pos != NULL) {
		/* Insert */
		reffect->next = reffect_pos;
		reffect_pos->prev = reffect;
		if (list->first_effect == reffect_pos)
			list->first_effect = reffect;
	} else {
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
}

/*
 * Error handling
 */

#undef sieve_result_error
void sieve_result_error(const struct sieve_action_exec_env *aenv,
			const char *csrc_filename, unsigned int csrc_linenum,
			const char *fmt, ...)
{
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_ERROR,
		.event = aenv->event,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
	};
	va_list args;

	va_start(args, fmt);
	sieve_logv(aenv->ehandler, &params, fmt, args);
	va_end(args);
}

#undef sieve_result_global_error
void sieve_result_global_error(const struct sieve_action_exec_env *aenv,
			       const char *csrc_filename,
			       unsigned int csrc_linenum, const char *fmt, ...)
{
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_ERROR,
		.event = aenv->event,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
	};
	va_list args;

	va_start(args, fmt);
	sieve_global_logv(eenv->svinst, aenv->ehandler, &params, fmt, args);
	va_end(args);
}

#undef sieve_result_warning
void sieve_result_warning(const struct sieve_action_exec_env *aenv,
			  const char *csrc_filename, unsigned int csrc_linenum,
			  const char *fmt, ...)
{
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_WARNING,
		.event = aenv->event,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
	};
	va_list args;

	va_start(args, fmt);
	sieve_logv(aenv->ehandler, &params, fmt, args);
	va_end(args);
}

#undef sieve_result_global_warning
void sieve_result_global_warning(const struct sieve_action_exec_env *aenv,
				 const char *csrc_filename,
				 unsigned int csrc_linenum,
				 const char *fmt, ...)
{
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_WARNING,
		.event = aenv->event,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
	};
	va_list args;

	va_start(args, fmt);
	sieve_global_logv(eenv->svinst, aenv->ehandler, &params, fmt, args);
	va_end(args);
}

#undef sieve_result_log
void sieve_result_log(const struct sieve_action_exec_env *aenv,
		      const char *csrc_filename, unsigned int csrc_linenum,
		      const char *fmt, ...)
{
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct sieve_error_params params = {
		.log_type = (HAS_ALL_BITS(eenv->flags,
					  SIEVE_EXECUTE_FLAG_LOG_RESULT) ?
			     LOG_TYPE_INFO : LOG_TYPE_DEBUG),
		.event = aenv->event,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
	};
	va_list args;

	va_start(args, fmt);
	sieve_logv(aenv->ehandler, &params, fmt, args);
	va_end(args);
}

#undef sieve_result_global_log
void sieve_result_global_log(const struct sieve_action_exec_env *aenv,
			     const char *csrc_filename,
			     unsigned int csrc_linenum, const char *fmt, ...)
{
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct sieve_error_params params = {
		.log_type = (HAS_ALL_BITS(eenv->flags,
					  SIEVE_EXECUTE_FLAG_LOG_RESULT) ?
			     LOG_TYPE_INFO : LOG_TYPE_DEBUG),
		.event = aenv->event,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
	};
	va_list args;

	va_start(args, fmt);
	sieve_global_logv(eenv->svinst, aenv->ehandler, &params, fmt, args);
	va_end(args);
}

#undef sieve_result_global_log_error
void sieve_result_global_log_error(const struct sieve_action_exec_env *aenv,
				   const char *csrc_filename,
				   unsigned int csrc_linenum,
				   const char *fmt, ...)
{
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_ERROR,
		.event = aenv->event,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
	};
	va_list args;

	va_start(args, fmt);
	sieve_global_info_logv(eenv->svinst, aenv->ehandler, &params,
			       fmt, args);
	va_end(args);
}

#undef sieve_result_global_log_warning
void sieve_result_global_log_warning(const struct sieve_action_exec_env *aenv,
				     const char *csrc_filename,
				     unsigned int csrc_linenum,
				     const char *fmt, ...)
{
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_WARNING,
		.event = aenv->event,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
	};
	va_list args;

	va_start(args, fmt);
	sieve_global_info_logv(eenv->svinst, aenv->ehandler, &params,
			       fmt, args);
	va_end(args);
}

#undef sieve_result_event_log
void sieve_result_event_log(const struct sieve_action_exec_env *aenv,
			    const char *csrc_filename,
			    unsigned int csrc_linenum, struct event *event,
			    const char *fmt, ...)
{
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct sieve_error_params params = {
		.log_type = (HAS_ALL_BITS(eenv->flags,
					  SIEVE_EXECUTE_FLAG_LOG_RESULT) ?
			     LOG_TYPE_INFO : LOG_TYPE_DEBUG),
		.event = event,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
	};
	va_list args;

	va_start(args, fmt);
	sieve_global_logv(eenv->svinst, aenv->ehandler, &params, fmt, args);
	va_end(args);
}


#undef sieve_result_critical
void sieve_result_critical(const struct sieve_action_exec_env *aenv,
			   const char *csrc_filename, unsigned int csrc_linenum,
			   const char *user_prefix, const char *fmt, ...)
{
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_ERROR,
		.event = aenv->event,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
	};
	va_list args;

	va_start(args, fmt);

	T_BEGIN {
		sieve_criticalv(eenv->svinst, aenv->ehandler, &params,
				user_prefix, fmt, args);
	} T_END;

	va_end(args);
}

#undef sieve_result_mail_error
int sieve_result_mail_error(const struct sieve_action_exec_env *aenv,
			    struct mail *mail,
			    const char *csrc_filename,
			    unsigned int csrc_linenum, const char *fmt, ...)
{
	const char *error_msg, *user_prefix;
	va_list args;

	error_msg = mailbox_get_last_error(mail->box, NULL);

	va_start(args, fmt);
	user_prefix = t_strdup_vprintf(fmt, args);
	sieve_result_critical(aenv, csrc_filename, csrc_linenum,
			      user_prefix, "%s: %s", user_prefix, error_msg);
	va_end(args);

	return 	SIEVE_EXEC_TEMP_FAILURE;
}
