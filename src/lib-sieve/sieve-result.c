/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "mempool.h"
#include "ostream.h"
#include "hash.h"
#include "str.h"
#include "llist.h"
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

enum sieve_action_execution_state {
	SIEVE_ACTION_EXECUTION_STATE_INIT = 0,
	SIEVE_ACTION_EXECUTION_STATE_STARTED,
	SIEVE_ACTION_EXECUTION_STATE_EXECUTED,
	SIEVE_ACTION_EXECUTION_STATE_FINALIZED,
};

struct sieve_result_action {
	struct sieve_action action;

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

	const struct sieve_execute_env *exec_env;
	struct sieve_error_handler *ehandler;
	struct sieve_message_context *msgctx;

	unsigned int exec_seq;
	struct sieve_result_execution *exec;

	struct sieve_action keep_action;
	struct sieve_action failure_action;

	unsigned int action_count;
	struct sieve_result_action *actions_head, *actions_tail;

	HASH_TABLE(const struct sieve_action_def *,
		   struct sieve_result_action_context *) action_contexts;
};

static const char *
sieve_result_event_log_message(struct sieve_result *result,
			       enum log_type log_type, const char *message)
{
	const struct sieve_script_env *senv = result->exec_env->scriptenv;

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

	result->exec_env = eenv;
	result->msgctx =
		sieve_message_context_create(svinst, senv->user, msgdata);

	result->keep_action.def = &act_store;
	result->keep_action.ext = NULL;
	result->failure_action.def = &act_store;
	result->failure_action.ext = NULL;

	result->action_count = 0;
	result->actions_head = NULL;
	result->actions_tail = NULL;

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

	sieve_message_context_unref(&result->msgctx);

	hash_table_destroy(&result->action_contexts);

	ract = result->actions_head;
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
	return result->exec_env->scriptenv;
}

const struct sieve_message_data *
sieve_result_get_message_data(struct sieve_result *result)
{
	return result->exec_env->msgdata;
}

struct sieve_message_context *
sieve_result_get_message_context(struct sieve_result *result)
{
	return result->msgctx;
}

unsigned int sieve_result_get_exec_seq(struct sieve_result *result)
{
	return result->exec_seq;
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
	void *const *ctx;

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

		i_assert(seffect->def != NULL);
		if (seffect->def->merge != NULL) {
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

		i_assert(nseffect->def != NULL);
		if (nseffect->def->merge != NULL) {
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
	if (result->actions_head == raction)
		result->actions_head = raction->next;

	if (result->actions_tail == raction)
		result->actions_tail = raction->prev;

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
	action.exec_seq = result->exec_seq;

	/* First, check for duplicates or conflicts */
	raction = result->actions_head;
	while (raction != NULL) {
		const struct sieve_action *oact = &raction->action;
		bool oact_new = (oact->exec_seq == result->exec_seq);

		if (keep && raction->action.keep) {
			/* Duplicate keep */
			if (oact->def == NULL || !oact_new) {
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
					if (keep && !oact->keep) {
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

				if (oact_new &&
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
		if (svinst->set->max_actions > 0 &&
		    result->action_count >= svinst->set->max_actions)
		{
			sieve_runtime_error(
				renv, action.location,
				"total number of actions exceeds policy limit "
				"(%u > %u)",
				result->action_count+1,
				svinst->set->max_actions);
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
		raction->seffects = seffects;
	}

	raction->action.name = (action.name == NULL ?
				act_def->name :
				p_strdup(result->pool, action.name));
	raction->action.context = context;
	raction->action.def = act_def;
	raction->action.ext = ext;
	raction->action.location = p_strdup(result->pool, action.location);
	raction->action.keep = keep;
	raction->action.exec_seq = result->exec_seq;

	if (raction->prev == NULL && raction != result->actions_head) {
		/* Add */
		if (result->actions_head == NULL) {
			result->actions_head = raction;
			result->actions_tail = raction;
			raction->prev = NULL;
			raction->next = NULL;
		} else {
			result->actions_tail->next = raction;
			raction->prev = result->actions_tail;
			result->actions_tail = raction;
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
		const struct sieve_side_effect *sef = &rsef->seffect;

		i_assert(sef->def != NULL);

		if (sef->def->print != NULL) {
			sef->def->print(sef, action, rpenv,
					implicit_keep);
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
	bool implicit_keep = TRUE, printed_any = FALSE;
	struct sieve_result_action *rac;

	if (keep != NULL)
		*keep = FALSE;

	/* Prepare environment */

	penv.result = result;
	penv.stream = stream;
	penv.scriptenv = senv;

	sieve_result_printf(&penv, "\nPerformed actions:\n\n");

	rac = result->actions_head;
	while (rac != NULL) {
		bool impl_keep = TRUE;
		const struct sieve_action *act = &rac->action;

		if (act->exec_seq < result->exec_seq) {
			rac = rac->next;
			continue;
		}

		if (rac->action.keep && keep != NULL)
			*keep = TRUE;

		if (act->def != NULL) {
			if (act->def->print != NULL)
				act->def->print(act, &penv, &impl_keep);
			else {
				sieve_result_action_printf(
					&penv, "%s", act->def->name);
			}
		} else {
			if (act->keep) {
				sieve_result_action_printf(&penv, "keep");
				impl_keep = FALSE;
			} else {
				sieve_result_action_printf(&penv, "[NULL]");
			}
		}
		printed_any = TRUE;

		/* Print side effects */
		sieve_result_print_side_effects(
			&penv, &rac->action, rac->seffects, &impl_keep);

		implicit_keep = implicit_keep && impl_keep;

		rac = rac->next;
	}
	if (!printed_any)
		sieve_result_printf(&penv, "  (none)\n");

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
			rac = result->actions_head;
			while (act_keep.def != NULL && rac != NULL) {
				if (rac->action.def == act_keep.def &&
				    act_keep.def->equals != NULL &&
				    act_keep.def->equals(senv, NULL, &rac->action) &&
				    sieve_action_is_executed(&rac->action,
							     result))
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

struct sieve_side_effect_execution {
	struct sieve_result_side_effect *seffect;

	void *tr_context;

	struct sieve_side_effect_execution *prev, *next;
};

struct sieve_action_execution {
	struct sieve_result_action *action;
	unsigned int exec_seq;
	struct sieve_action_execution *prev, *next;

	struct sieve_side_effect_execution *seffects_head, *seffects_tail;

	struct sieve_error_handler *ehandler;
	void *tr_context;
	enum sieve_action_execution_state state;
	int status;

	bool commit:1;
};

struct sieve_result_execution {
	pool_t pool;
	struct sieve_action_exec_env action_env;
	struct sieve_error_handler *ehandler;
	struct event *event;

	int status;

	struct sieve_action_execution *actions_head, *actions_tail;

	struct sieve_result_action keep_action;
	struct sieve_action_execution keep;
	struct sieve_action_execution *keep_equiv_action;
	int keep_status;

	bool keep_success:1;
	bool keep_explicit:1;
	bool keep_implicit:1;
	bool keep_finalizing:1;
	bool seen_delivery:1;
	bool executed:1;
	bool executed_delivery:1;
	bool committed:1;
};

void sieve_result_mark_executed(struct sieve_result *result)
{
	result->exec_seq++;
}

/* Side effect */

static int
sieve_result_side_effect_pre_execute(struct sieve_result_execution *rexec,
				     struct sieve_action_execution *aexec,
				     struct sieve_side_effect_execution *seexec)
{
	struct sieve_result_side_effect *rsef = seexec->seffect;
	struct sieve_side_effect *sef = &rsef->seffect;

	i_assert(sef->def != NULL);
	if (sef->def->pre_execute == NULL)
		return SIEVE_EXEC_OK;

	return sef->def->pre_execute(sef, &rexec->action_env,
				     aexec->tr_context, &seexec->tr_context);
}

static int
sieve_result_side_effect_post_execute(
	struct sieve_result_execution *rexec,
	struct sieve_action_execution *aexec,
	struct sieve_side_effect_execution *seexec, bool *impl_keep)
{
	struct sieve_result_side_effect *rsef = seexec->seffect;
	struct sieve_side_effect *sef = &rsef->seffect;

	i_assert(sef->def != NULL);
	if (sef->def->post_execute == NULL)
		return SIEVE_EXEC_OK;

	return sef->def->post_execute(sef, &rexec->action_env,
				      aexec->tr_context, seexec->tr_context,
				      impl_keep);
}

static void
sieve_result_side_effect_post_commit(struct sieve_result_execution *rexec,
				     struct sieve_action_execution *aexec,
				     struct sieve_side_effect_execution *seexec,
				     int commit_status)
{
	struct sieve_result_side_effect *rsef = seexec->seffect;
	struct sieve_side_effect *sef = &rsef->seffect;

	i_assert(sef->def != NULL);
	if (sef->def->post_commit == NULL)
		return;

	sef->def->post_commit(sef, &rexec->action_env,
			      aexec->tr_context, seexec->tr_context,
			      commit_status);
}

static void
sieve_result_side_effect_rollback(struct sieve_result_execution *rexec,
				  struct sieve_action_execution *aexec,
				  struct sieve_side_effect_execution *seexec)
{
	struct sieve_result_side_effect *rsef = seexec->seffect;
	struct sieve_side_effect *sef = &rsef->seffect;

	i_assert(sef->def != NULL);
	if (sef->def->rollback == NULL)
		return;

	sef->def->rollback(sef, &rexec->action_env,
			   aexec->tr_context, seexec->tr_context,
			   (aexec->status == SIEVE_EXEC_OK));
}

static void
sieve_action_execution_add_side_effect(struct sieve_result_execution *rexec,
				       struct sieve_action_execution *aexec,
				       struct sieve_result_side_effect *seffect)
{
	struct sieve_side_effect_execution *seexec;

	seexec = aexec->seffects_head;
	while (seexec != NULL) {
		if (seexec->seffect == seffect)
			return;
		seexec = seexec->next;
	}

	seexec = p_new(rexec->pool, struct sieve_side_effect_execution, 1);
	seexec->seffect = seffect;

	DLLIST2_APPEND(&aexec->seffects_head, &aexec->seffects_tail, seexec);
}

static void
sieve_action_execution_add_side_effects(struct sieve_result_execution *rexec,
				        struct sieve_action_execution *aexec,
					struct sieve_result_action *rac)
{
	struct sieve_result_side_effect *rsef;

	rsef = (rac->seffects == NULL ? NULL : rac->seffects->first_effect);
	while (rsef != NULL) {
		sieve_action_execution_add_side_effect(rexec, aexec, rsef);
		rsef = rsef->next;
	}
}

/* Action */

static void
sieve_action_execution_pre(struct sieve_result_execution *rexec,
			   struct sieve_action_execution *aexec)
{
	if (aexec->ehandler == NULL)
		aexec->ehandler = rexec->ehandler;
	rexec->action_env.action = &aexec->action->action;
	rexec->action_env.event = aexec->action->action.event;
	rexec->action_env.ehandler = aexec->ehandler;
}

static void
sieve_action_execution_post(struct sieve_result_execution *rexec)
{
	rexec->action_env.action = NULL;
	rexec->action_env.event = rexec->action_env.result->event;
	rexec->action_env.ehandler = NULL;
}

static int
sieve_result_action_start(struct sieve_result_execution *rexec,
			  struct sieve_action_execution *aexec)
{
	struct sieve_result_action *rac = aexec->action;
	struct sieve_action *act = &rac->action;
	int status = SIEVE_EXEC_OK;

	/* Skip actions that are already started. */
	if (aexec->state >= SIEVE_ACTION_EXECUTION_STATE_STARTED)
		return status;
	aexec->state = SIEVE_ACTION_EXECUTION_STATE_STARTED;
	aexec->status = status;

	/* Skip non-actions (inactive keep). */
	if (act->def == NULL)
		return status;

	if (act->def->start != NULL) {
		sieve_action_execution_pre(rexec, aexec);
		status = act->def->start(&rexec->action_env,
					 &aexec->tr_context);
		aexec->status = status;
		sieve_action_execution_post(rexec);
	}
	return status;
}

static int
sieve_result_action_execute(struct sieve_result_execution *rexec,
			    struct sieve_action_execution *aexec,
			    int start_status)
{
	struct sieve_result_action *rac = aexec->action;
	struct sieve_action *act = &rac->action;
	struct sieve_side_effect_execution *seexec;
	int status = start_status;
	bool impl_keep = TRUE;

	/* Skip actions that are already executed. */
	if (aexec->state >= SIEVE_ACTION_EXECUTION_STATE_EXECUTED)
		return status;
	aexec->state = SIEVE_ACTION_EXECUTION_STATE_EXECUTED;

	/* Record explicit keep when it is not the final implicit keep */
	if (act->keep && aexec != &rexec->keep)
		rexec->keep_explicit = TRUE;

	/* Skip non-actions (inactive keep) */
	if (act->def == NULL) {
		i_assert(aexec != &rexec->keep);
		if (act->keep)
			e_debug(rexec->event, "Executed explicit keep");
		return status;
	}

	/* Don't execute if others already failed */
	if (status != SIEVE_EXEC_OK)
		return status;

	if (aexec == &rexec->keep)
		e_debug(rexec->event, "Executing implicit keep action");
	else {
		e_debug(rexec->event, "Executing %s action%s",
			sieve_action_name(act),
			(act->keep ? " (explicit keep)" : ""));
	}

	sieve_action_execution_pre(rexec, aexec);

	/* Execute pre-execute event of side effects */
	seexec = aexec->seffects_head;
	while (status == SIEVE_EXEC_OK && seexec != NULL) {
		status = sieve_result_side_effect_pre_execute(
			rexec, aexec, seexec);
		seexec = seexec->next;
	}

	/* Execute the action itself */
	if (status == SIEVE_EXEC_OK && act->def != NULL &&
	    act->def->execute != NULL) {
		status = act->def->execute(&rexec->action_env,
					   aexec->tr_context,
					   &impl_keep);
		if (status == SIEVE_EXEC_OK)
			rexec->executed = TRUE;
	}

	/* Execute post-execute event of side effects */
	seexec = aexec->seffects_head;
	while (status == SIEVE_EXEC_OK && seexec != NULL) {
		status = sieve_result_side_effect_post_execute(
			rexec, aexec, seexec, &impl_keep);
		seexec = seexec->next;
	}

	if (aexec == &rexec->keep) {
		e_debug(rexec->event,
			"Finished executing implicit keep action (status=%s)",
			sieve_execution_exitcode_to_str(status));
	} else {
		e_debug(rexec->event, "Finished executing %s action "
			"(status=%s, keep=%s)", sieve_action_name(act),
			sieve_execution_exitcode_to_str(status),
			(act->keep ? "explicit" :
			 (impl_keep && rexec->keep_implicit ?
			  "implicit" : "canceled")));
	}

	if (status == SIEVE_EXEC_OK && act->def != NULL &&
	    (act->def->flags & SIEVE_ACTFLAG_TRIES_DELIVER) != 0)
		rexec->seen_delivery = TRUE;

	/* Update implicit keep status (but only when we're not running the
	   implicit keep right now). */
	if (aexec != &rexec->keep)
		rexec->keep_implicit = rexec->keep_implicit && impl_keep;

	sieve_action_execution_post(rexec);

	aexec->status = status;
	return status;
}

static int
sieve_result_action_commit(struct sieve_result_execution *rexec,
			   struct sieve_action_execution *aexec)
{
	struct sieve_result_action *rac = aexec->action;
	struct sieve_action *act = &rac->action;
	struct sieve_side_effect_execution *seexec;
	int cstatus = SIEVE_EXEC_OK;

	if (aexec == &rexec->keep) {
		e_debug(rexec->event, "Commit implicit keep action");
	} else {
		e_debug(rexec->event, "Commit %s action%s",
			sieve_action_name(act),
			(act->keep ? " (explicit keep)" : ""));
	}

	sieve_action_execution_pre(rexec, aexec);

	if (act->def->commit != NULL) {
		cstatus = act->def->commit(&rexec->action_env,
					   aexec->tr_context);
		if (cstatus == SIEVE_EXEC_OK)
			rexec->committed = TRUE;
	}

	/* Execute post_commit event of side effects */
	seexec = aexec->seffects_head;
	while (seexec != NULL) {
		sieve_result_side_effect_post_commit(
			rexec, aexec, seexec, cstatus);
		seexec = seexec->next;
	}

	sieve_action_execution_post(rexec);

	return cstatus;
}

static void
sieve_result_action_rollback(struct sieve_result_execution *rexec,
			     struct sieve_action_execution *aexec)
{
	struct sieve_result_action *rac = aexec->action;
	struct sieve_action *act = &rac->action;
	struct sieve_side_effect_execution *seexec;

	if (aexec == &rexec->keep) {
		e_debug(rexec->event, "Roll back implicit keep action");
	} else {
		e_debug(rexec->event, "Roll back %s action%s",
			sieve_action_name(act),
			(act->keep ? " (explicit keep)" : ""));
	}

	sieve_action_execution_pre(rexec, aexec);

	if (act->def->rollback != NULL) {
		act->def->rollback(&rexec->action_env, aexec->tr_context,
				   (aexec->status == SIEVE_EXEC_OK));
	}

	/* Rollback side effects */
	seexec = aexec->seffects_head;
	while (seexec != NULL) {
		sieve_result_side_effect_rollback(rexec, aexec, seexec);
		seexec = seexec->next;
	}

	sieve_action_execution_post(rexec);
}

static int
sieve_result_action_commit_or_rollback(struct sieve_result_execution *rexec,
				       struct sieve_action_execution *aexec,
				       int status, int *commit_status)
{
	struct sieve_result_action *rac = aexec->action;
	struct sieve_action *act = &rac->action;
	const struct sieve_execute_env *exec_env = rexec->action_env.exec_env;

	/* Skip actions that are already finalized. */
	if (aexec->state >= SIEVE_ACTION_EXECUTION_STATE_FINALIZED)
		return status;
	aexec->state = SIEVE_ACTION_EXECUTION_STATE_FINALIZED;

	if (aexec == &rexec->keep) {
		e_debug(rexec->event, "Finalize implicit keep action"
			"(status=%s, action_status=%s, commit_status=%s)",
			sieve_execution_exitcode_to_str(status),
			sieve_execution_exitcode_to_str(aexec->status),
			sieve_execution_exitcode_to_str(*commit_status));
	} else {
		e_debug(rexec->event, "Finalize %s action "
			"(%sstatus=%s, action_status=%s, commit_status=%s, "
			 "pre-commit=%s)",
			sieve_action_name(act),
			(act->keep ? "explicit keep, " : ""),
			sieve_execution_exitcode_to_str(status),
			sieve_execution_exitcode_to_str(aexec->status),
			sieve_execution_exitcode_to_str(*commit_status),
			(aexec->commit ? "yes" : "no"));
	}

	/* Skip non-actions (inactive keep) */
	if (act->def == NULL)
		return status;

	if (aexec->status == SIEVE_EXEC_OK &&
	    (status == SIEVE_EXEC_OK ||
	     (aexec->commit && *commit_status == SIEVE_EXEC_OK))) {
		int cstatus = SIEVE_EXEC_OK;

		cstatus = sieve_result_action_commit(rexec, aexec);
		if (cstatus != SIEVE_EXEC_OK) {
			/* This is bad; try to salvage as much as possible */
			if (*commit_status == SIEVE_EXEC_OK) {
				*commit_status = cstatus;
				if (!rexec->committed ||
				    exec_env->exec_status->store_failed) {
					/* We haven't executed anything yet,
					   or storing mail locally failed;
					   continue as rollback. We generally
					   don't want to fail entirely, e.g.
					   a failed mail forward shouldn't
					   cause duplicate local deliveries. */
					status = cstatus;
				}
			}
		}
	} else {
		sieve_result_action_rollback(rexec, aexec);
	}

	if (act->keep) {
		if (status == SIEVE_EXEC_FAILURE)
			status = SIEVE_EXEC_KEEP_FAILED;
		if (*commit_status == SIEVE_EXEC_FAILURE)
			*commit_status = SIEVE_EXEC_KEEP_FAILED;
	}

	return status;
}

static void
sieve_result_action_finish(struct sieve_result_execution *rexec,
			   struct sieve_action_execution *aexec, int status)
{
	struct sieve_result_action *rac = aexec->action;
	struct sieve_action *act = &rac->action;

	/* Skip non-actions (inactive keep) */
	if (act->def == NULL)
		return;

	if (aexec == &rexec->keep) {
		e_debug(rexec->event, "Finish implicit keep action");
	} else {
		e_debug(rexec->event, "Finish %s action%s",
			sieve_action_name(act),
			(act->keep ? " (explicit keep)" : ""));
	}

	if (act->def->finish != NULL) {
		sieve_action_execution_pre(rexec, aexec);
		act->def->finish(&rexec->action_env, aexec->tr_context, status);
		sieve_action_execution_post(rexec);
	}
}

static void
sieve_result_action_abort(struct sieve_result_execution *rexec,
			  struct sieve_action_execution *aexec)
{
	if (aexec->state > SIEVE_ACTION_EXECUTION_STATE_INIT &&
	    aexec->state < SIEVE_ACTION_EXECUTION_STATE_FINALIZED)
		sieve_result_action_rollback(rexec, aexec);
	DLLIST2_REMOVE(&rexec->actions_head, &rexec->actions_tail, aexec);
}

static void
sieve_action_execution_update(struct sieve_result_execution *rexec,
			      struct sieve_action_execution *aexec)
{
	const struct sieve_action_exec_env *aenv = &rexec->action_env;
	struct sieve_result *result = aenv->result;
	struct sieve_result_action *rac;

	rac = result->actions_head;
	while (rac != NULL) {
		if (aexec->action == rac)
			break;
		rac = rac->next;
	}

	if (rac == NULL) {
		/* Action was removed; abort it. */
		sieve_result_action_abort(rexec, aexec);
		return;
	}

	if (aexec->exec_seq != rac->action.exec_seq) {
		i_assert(rac->action.keep);

		/* Recycled keep */
		aexec->exec_seq = rac->action.exec_seq;
		aexec->state = SIEVE_ACTION_EXECUTION_STATE_INIT;
	}

	sieve_action_execution_add_side_effects(rexec, aexec, rac);
}

static void
sieve_result_execution_add_action(struct sieve_result_execution *rexec,
				  struct sieve_result_action *rac)
{
	struct sieve_action_execution *aexec;

	aexec = rexec->actions_head;
	while (aexec != NULL) {
		if (aexec->action == rac)
			return;
		aexec = aexec->next;
	}

	aexec = p_new(rexec->pool, struct sieve_action_execution, 1);
	aexec->action = rac;
	aexec->exec_seq = rac->action.exec_seq;
	aexec->ehandler = rexec->ehandler;

	DLLIST2_APPEND(&rexec->actions_head, &rexec->actions_tail, aexec);

	sieve_action_execution_add_side_effects(rexec, aexec, rac);
}

/* Result */

struct sieve_result_execution *
sieve_result_execution_create(struct sieve_result *result, pool_t pool)
{
	struct sieve_result_execution *rexec;

	pool_ref(pool);
	rexec = p_new(pool, struct sieve_result_execution, 1);
	rexec->pool = pool;
	rexec->event = result->event;
	rexec->action_env.result = result;
	rexec->action_env.event = result->event;
	rexec->action_env.exec_env = result->exec_env;
	rexec->action_env.msgctx = result->msgctx;
	rexec->status = SIEVE_EXEC_OK;
	rexec->keep_success = TRUE;
	rexec->keep_status = SIEVE_EXEC_OK;
	rexec->keep_explicit = FALSE;
	rexec->keep_implicit = TRUE;

	sieve_result_ref(result);
	result->exec = rexec;

	return rexec;
}

void sieve_result_execution_destroy(struct sieve_result_execution **_rexec)
{
	struct sieve_result_execution *rexec = *_rexec;

	*_rexec = NULL;

	if (rexec == NULL)
		return;

	rexec->action_env.result->exec = NULL;
	sieve_result_unref(&rexec->action_env.result);
	pool_unref(&rexec->pool);
}

static void
sieve_result_implicit_keep_execute(struct sieve_result_execution *rexec)
{
	const struct sieve_action_exec_env *aenv = &rexec->action_env;
	struct sieve_result *result = aenv->result;
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct sieve_action_execution *aexec;
	int status = SIEVE_EXEC_OK;
	struct sieve_action_execution *aexec_keep = &rexec->keep;
	struct sieve_result_action *ract_keep = &rexec->keep_action;
	struct sieve_action *act_keep = &ract_keep->action;
	bool success = FALSE;

	switch (rexec->status) {
	case SIEVE_EXEC_OK:
		success = TRUE;
		break;
	case SIEVE_EXEC_TEMP_FAILURE:
	case SIEVE_EXEC_RESOURCE_LIMIT:
		if (rexec->committed) {
			e_debug(rexec->event,
				"Temporary failure occurred (status=%s), "
				"but other actions were already committed: "
				"execute failure implicit keep",
				sieve_execution_exitcode_to_str(rexec->status));
			break;
		}
		if (rexec->keep_finalizing)
			break;

		e_debug(rexec->event,
			"Skip implicit keep for temporary failure "
			"(state=execute, status=%s)",
			sieve_execution_exitcode_to_str(rexec->status));
		return;
	default:
		break;
	}

	if (rexec->keep_equiv_action != NULL) {
		e_debug(rexec->event, "No implicit keep needed "
			"(equivalent action already executed)");
		return;
	}

	rexec->keep.action = &rexec->keep_action;
	rexec->keep.ehandler = rexec->ehandler;
	rexec->keep_success = success;
	rexec->keep_status = status;

	if ((eenv->flags & SIEVE_EXECUTE_FLAG_DEFER_KEEP) != 0) {
		e_debug(rexec->event, "Execution of implicit keep is deferred");
		return;
	}

	if (!success)
		*act_keep = result->failure_action;
	else
		*act_keep = result->keep_action;
	act_keep->name = "keep";
	act_keep->mail = NULL;
	act_keep->keep = TRUE;

	/* If keep is a non-action, return right away */
	if (act_keep->def == NULL) {
		e_debug(rexec->event, "Keep is not defined yet");
		return;
	}

	/* Scan for execution of keep-equal actions */
	aexec = rexec->actions_head;
	while (aexec != NULL) {
		struct sieve_result_action *rac = aexec->action;

		if (rac->action.def == act_keep->def &&
		    act_keep->def->equals != NULL &&
		    act_keep->def->equals(eenv->scriptenv, NULL,
					  &rac->action) &&
		    aexec->state >= SIEVE_ACTION_EXECUTION_STATE_EXECUTED) {
			e_debug(rexec->event, "No implicit keep needed "
				"(equivalent %s action already executed)",
				sieve_action_name(&rac->action));
			rexec->keep_equiv_action = aexec;
			return;
		}

		aexec = aexec->next;
	}

	/* Scan for deferred keep */
	aexec = rexec->actions_tail;
	while (aexec != NULL) {
		struct sieve_result_action *rac = aexec->action;

		if (aexec->state < SIEVE_ACTION_EXECUTION_STATE_EXECUTED) {
			aexec = NULL;
			break;
		}
		if (rac->action.keep && rac->action.def == NULL)
			break;
		aexec = aexec->prev;
	}

	if (aexec == NULL) {
		if (success)
			act_keep->mail = sieve_message_get_mail(aenv->msgctx);
	} else {
		e_debug(rexec->event, "Found deferred keep action");

		if (success) {
			act_keep->location = aexec->action->action.location;
			act_keep->mail = aexec->action->action.mail;
			ract_keep->seffects = aexec->action->seffects;
		}
		aexec->state = SIEVE_ACTION_EXECUTION_STATE_FINALIZED;
	}

	if (ract_keep->seffects == NULL) {
		/* Apply any implicit side effects if applicable */
		if (success && hash_table_is_created(result->action_contexts)) {
			struct sieve_result_action_context *actctx;

			/* Check for implicit side effects to keep action */
			actctx = hash_table_lookup(result->action_contexts,
						   act_keep->def);

			if (actctx != NULL)
				ract_keep->seffects = actctx->seffects;
		}
	}

	e_debug(rexec->event, "Execute implicit keep (status=%s)",
		sieve_execution_exitcode_to_str(rexec->status));

	/* Initialize side effects */
	sieve_action_execution_add_side_effects(rexec, aexec_keep, ract_keep);

	/* Initialize keep action event */
	sieve_result_init_action_event(result, act_keep, FALSE);

	/* Start keep action */
	status = sieve_result_action_start(rexec, aexec_keep);

	/* Execute keep action */
	if (status == SIEVE_EXEC_OK)
		status = sieve_result_action_execute(rexec, aexec_keep, status);
	if (status == SIEVE_EXEC_OK)
		aexec_keep->commit = TRUE;

	rexec->executed_delivery = rexec->seen_delivery;
	rexec->keep_status = status;
	sieve_action_execution_post(rexec);
}

static int
sieve_result_implicit_keep_finalize(struct sieve_result_execution *rexec)
{
	const struct sieve_action_exec_env *aenv = &rexec->action_env;
	const struct sieve_execute_env *eenv = aenv->exec_env;
	struct sieve_action_execution *aexec_keep = &rexec->keep;
	struct sieve_result_action *ract_keep = &rexec->keep_action;
	struct sieve_action *act_keep = &ract_keep->action;
	int commit_status = SIEVE_EXEC_OK;
	bool success = FALSE, temp_failure = FALSE;

	switch (rexec->status) {
	case SIEVE_EXEC_OK:
		success = TRUE;
		break;
	case SIEVE_EXEC_TEMP_FAILURE:
	case SIEVE_EXEC_RESOURCE_LIMIT:
		if (rexec->committed) {
			e_debug(rexec->event,
				"Temporary failure occurred (status=%s), "
				"but other actions were already committed: "
				"commit failure implicit keep",
				sieve_execution_exitcode_to_str(rexec->status));
			break;
		}

		if (aexec_keep->state !=
		    SIEVE_ACTION_EXECUTION_STATE_EXECUTED) {
			e_debug(rexec->event,
				"Skip implicit keep for temporary failure "
				"(state=commit, status=%s)",
				sieve_execution_exitcode_to_str(rexec->status));
			return rexec->status;
		}
		/* Roll back for temporary failure when no other action
		   is committed. */
		commit_status = rexec->status;
		temp_failure = TRUE;
		break;
	default:
		break;
	}

	if ((eenv->flags & SIEVE_EXECUTE_FLAG_DEFER_KEEP) != 0) {
		e_debug(rexec->event, "Execution of implicit keep is deferred");
		return rexec->keep_status;
	}

	rexec->keep_finalizing = TRUE;

	/* Start keep if necessary */
	if (temp_failure) {
		rexec->keep_status = rexec->status;
	} else if (act_keep->def == NULL ||
		   aexec_keep->state != SIEVE_ACTION_EXECUTION_STATE_EXECUTED) {
		sieve_result_implicit_keep_execute(rexec);
	/* Switch to failure keep if necessary. */
	} else if (rexec->keep_success && !success){
		e_debug(rexec->event, "Switch to failure implicit keep");

		/* Failed transaction, rollback success keep action. */
		sieve_result_action_rollback(rexec, aexec_keep);

		event_unref(&act_keep->event);
		i_zero(aexec_keep);

		/* Start failure keep action. */
		sieve_result_implicit_keep_execute(rexec);
	}
	if (act_keep->def == NULL)
		return rexec->keep_status;

	if (rexec->keep_equiv_action != NULL) {
		struct sieve_action_execution *ke_aexec =
			rexec->keep_equiv_action;

		i_assert(ke_aexec->state >=
			 SIEVE_ACTION_EXECUTION_STATE_FINALIZED);

		e_debug(rexec->event, "No implicit keep needed "
			"(equivalent %s action already finalized)",
			sieve_action_name(&ke_aexec->action->action));
		return ke_aexec->status;
	}

	e_debug(rexec->event, "Finalize implicit keep (status=%s)",
		sieve_execution_exitcode_to_str(rexec->status));

	i_assert(aexec_keep->state == SIEVE_ACTION_EXECUTION_STATE_EXECUTED);

	/* Finalize keep action */
	rexec->keep_status = sieve_result_action_commit_or_rollback(
		rexec, aexec_keep, rexec->keep_status, &commit_status);

	/* Finish keep action */
	sieve_result_action_finish(rexec, aexec_keep,
				   rexec->keep_status);

	sieve_action_execution_post(rexec);
	event_unref(&act_keep->event);

	if (rexec->keep_status == SIEVE_EXEC_FAILURE)
		rexec->keep_status = SIEVE_EXEC_KEEP_FAILED;
	return rexec->keep_status;
}

bool sieve_result_executed(struct sieve_result_execution *rexec)
{
	return rexec->executed;
}

bool sieve_result_committed(struct sieve_result_execution *rexec)
{
	return rexec->committed;
}

bool sieve_result_executed_delivery(struct sieve_result_execution *rexec)
{
	return rexec->executed_delivery;
}

static int sieve_result_transaction_start(struct sieve_result_execution *rexec)
{
	struct sieve_action_execution *aexec;
	int status = SIEVE_EXEC_OK;

	e_debug(rexec->event, "Starting execution of actions");

	aexec = rexec->actions_head;
	while (status == SIEVE_EXEC_OK && aexec != NULL) {
		status = sieve_result_action_start(rexec, aexec);
		aexec = aexec->next;
	}
	sieve_action_execution_post(rexec);

	return status;
}

static int
sieve_result_transaction_execute(struct sieve_result_execution *rexec,
				 int start_status)
{
	struct sieve_action_execution *aexec;
	int status = SIEVE_EXEC_OK;

	e_debug(rexec->event, "Executing actions");

	rexec->seen_delivery = FALSE;
	aexec = rexec->actions_head;
	while (status == SIEVE_EXEC_OK && aexec != NULL) {
		status = sieve_result_action_execute(rexec, aexec,
						     start_status);
		aexec = aexec->next;
	}
	sieve_action_execution_post(rexec);

	if (status == SIEVE_EXEC_OK) {
		/* Since this execution series is successful so far, mark all
		   actions in it to be committed. */
		aexec = rexec->actions_head;
		while (aexec != NULL) {
			aexec->commit = TRUE;
			aexec = aexec->next;
		}

		rexec->executed_delivery =
			rexec->executed_delivery || rexec->seen_delivery;
	}

	e_debug(rexec->event, "Finished executing actions "
		"(status=%s, keep=%s, executed=%s)",
		sieve_execution_exitcode_to_str(status),
		(rexec->keep_explicit ? "explicit" :
		 (rexec->keep_implicit ? "implicit" : "none")),
		(rexec->executed ? "yes" : "no"));
	return status;
}

static int
sieve_result_transaction_commit_or_rollback(
	struct sieve_result_execution *rexec, int status)
{
	struct sieve_action_execution *aexec;
	int commit_status = SIEVE_EXEC_OK;

	switch (status) {
	case SIEVE_EXEC_TEMP_FAILURE:
		/* Roll back all actions */
		commit_status = status;
		break;
	default:
		break;
	}

	e_debug(rexec->event, "Finalizing actions");

	/* First commit/rollback all storage actions */
	aexec = rexec->actions_head;
	while (aexec != NULL) {
		struct sieve_result_action *rac = aexec->action;
		struct sieve_action *act = &rac->action;

		if (act->def == NULL ||
		    (act->def->flags & SIEVE_ACTFLAG_MAIL_STORAGE) == 0) {
			aexec = aexec->next;
			continue;
		}

		status = sieve_result_action_commit_or_rollback(
			rexec, aexec, status, &commit_status);

		aexec = aexec->next;
	}

	/* Then commit/rollback all other actions */
	aexec = rexec->actions_head;
	while (aexec != NULL) {
		struct sieve_result_action *rac = aexec->action;
		struct sieve_action *act = &rac->action;

		if (act->def != NULL &&
		    (act->def->flags & SIEVE_ACTFLAG_MAIL_STORAGE) != 0) {
			aexec = aexec->next;
			continue;
		}

		status = sieve_result_action_commit_or_rollback(
			rexec, aexec, status, &commit_status);

		aexec = aexec->next;
	}

	e_debug(rexec->event, "Finished finalizing actions "
		"(status=%s, keep=%s, committed=%s)",
		sieve_execution_exitcode_to_str(status),
		(rexec->keep_explicit ? "explicit" :
		 (rexec->keep_implicit ? "implicit" : "none")),
		(rexec->committed ? "yes" : "no"));

	return commit_status;
}

static void
sieve_result_transaction_finish(struct sieve_result_execution *rexec,
				int status)
{
	struct sieve_action_execution *aexec;

	e_debug(rexec->event, "Finishing actions");

	aexec = rexec->actions_head;
	while (aexec != NULL) {
		sieve_result_action_finish(rexec, aexec, status);
		aexec = aexec->next;
	}
	sieve_action_execution_post(rexec);
}

static void
sieve_result_execute_update_status(struct sieve_result_execution *rexec,
				   int status)
{
	switch (status) {
	case SIEVE_EXEC_OK:
		break;
	case SIEVE_EXEC_TEMP_FAILURE:
		rexec->status = status;
		break;
	case SIEVE_EXEC_BIN_CORRUPT:
		i_unreached();
	case SIEVE_EXEC_FAILURE:
	case SIEVE_EXEC_KEEP_FAILED:
		if (rexec->status == SIEVE_EXEC_OK)
			rexec->status = status;
		break;
	case SIEVE_EXEC_RESOURCE_LIMIT:
		if (rexec->status != SIEVE_EXEC_TEMP_FAILURE)
			rexec->status = status;
		break;
	}
}

static void
sieve_result_execution_update(struct sieve_result_execution *rexec)
{
	const struct sieve_action_exec_env *aenv = &rexec->action_env;
	struct sieve_result *result = aenv->result;
	struct sieve_action_execution *aexec;
	struct sieve_result_action *rac;

	aexec = rexec->actions_head;
	while (aexec != NULL) {
		struct sieve_action_execution *aexec_next = aexec->next;

		sieve_action_execution_update(rexec, aexec);
		aexec = aexec_next;
	}

	rac = result->actions_head;
	while (rac != NULL) {
		sieve_result_execution_add_action(rexec, rac);
		rac = rac->next;
	}
}

int sieve_result_execute(struct sieve_result_execution *rexec, int status,
			 bool commit, struct sieve_error_handler *ehandler,
			 bool *keep_r)
{
	const struct sieve_action_exec_env *aenv = &rexec->action_env;
	struct sieve_result *result = aenv->result;
	int result_status, ret;

	e_debug(rexec->event, "Executing result (status=%s, commit=%s)",
		sieve_execution_exitcode_to_str(status),
		(commit ? "yes" : "no"));

	if (keep_r != NULL)
		*keep_r = FALSE;
	sieve_result_mark_executed(result);

	/* Prepare environment */

	rexec->ehandler = ehandler;

	/* Update actions in execution from result */

	sieve_result_execution_update(rexec);

	/* Transaction start and execute */

	if (status != SIEVE_EXEC_OK) {
		sieve_result_execute_update_status(rexec, status);
	} else if (rexec->status == SIEVE_EXEC_OK) {
		/* Transaction start */

		status = sieve_result_transaction_start(rexec);

		/* Transaction execute */

		status = sieve_result_transaction_execute(rexec, status);
		sieve_result_execute_update_status(rexec, status);
	}

	if (!commit) {
		sieve_action_execution_post(rexec);
		rexec->ehandler = NULL;

		/* Merge explicit keep status into implicit keep for the next
		   execution round. */
		rexec->keep_implicit = (rexec->keep_explicit ||
					rexec->keep_implicit);
		rexec->keep_explicit = FALSE;

		e_debug(rexec->event, "Finished executing result "
			"(no commit, status=%s, keep=%s)",
			sieve_execution_exitcode_to_str(rexec->status),
			(rexec->keep_implicit ? "yes" : "no"));

		if (keep_r != NULL)
			*keep_r = rexec->keep_implicit;
		return rexec->status;
	}

	/* Execute implicit keep if the transaction failed or when the
	   implicit keep was not canceled during transaction.
	 */
	if (rexec->status != SIEVE_EXEC_OK || rexec->keep_implicit)
		sieve_result_implicit_keep_execute(rexec);

	/* Transaction commit/rollback */

	status = sieve_result_transaction_commit_or_rollback(rexec, status);
	sieve_result_execute_update_status(rexec, status);

	/* Commit implicit keep if necessary */

	result_status = rexec->status;

	/* Commit implicit keep if the transaction failed or when the
	   implicit keep was not canceled during transaction.
	 */
	if (rexec->status != SIEVE_EXEC_OK || rexec->keep_implicit) {
		ret = sieve_result_implicit_keep_finalize(rexec);
		switch (ret) {
		case SIEVE_EXEC_OK:
			if (result_status == SIEVE_EXEC_TEMP_FAILURE)
				result_status = SIEVE_EXEC_FAILURE;
			break;
		case SIEVE_EXEC_TEMP_FAILURE:
		case SIEVE_EXEC_RESOURCE_LIMIT:
			if (!rexec->committed) {
				result_status = ret;
				break;
			}
			/* fall through */
		default:
			result_status = SIEVE_EXEC_KEEP_FAILED;
		}
	}
	if (rexec->status == SIEVE_EXEC_OK)
		rexec->status = result_status;

	/* Finish execution */

	sieve_result_transaction_finish(rexec, rexec->status);

	sieve_action_execution_post(rexec);
	rexec->ehandler = NULL;

	rexec->status = result_status;

	/* Merge explicit keep status into implicit keep (in this case only for
	   completeness).
	 */
	rexec->keep_implicit = (rexec->keep_explicit ||
				rexec->keep_implicit);
	rexec->keep_explicit = FALSE;

	e_debug(rexec->event, "Finished executing result "
		"(final, status=%s, keep=%s)",
		sieve_execution_exitcode_to_str(result_status),
		(rexec->keep_implicit ? "yes" : "no"));

	if (keep_r != NULL)
		*keep_r = rexec->keep_implicit;
	return result_status;
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
	rictx->next_action = result->actions_head;

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
			*keep = rac->action.keep;

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
		result->actions_head = rac->next;
	else
		rac->prev->next = rac->next;

	if (rac->next == NULL)
		result->actions_tail = rac->prev;
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

		i_assert(ref_def != NULL);
		i_assert(sef_def != NULL);

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

	error_msg = mailbox_get_last_internal_error(mail->box, NULL);

	va_start(args, fmt);
	user_prefix = t_strdup_vprintf(fmt, args);
	sieve_result_critical(aenv, csrc_filename, csrc_linenum,
			      user_prefix, "%s: %s", user_prefix, error_msg);
	va_end(args);

	return 	SIEVE_EXEC_TEMP_FAILURE;
}
