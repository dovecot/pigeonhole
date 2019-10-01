#ifndef SIEVE_ACTIONS_H
#define SIEVE_ACTIONS_H

#include "lib.h"
#include "mail-types.h"
#include "mail-error.h"

#include "sieve-common.h"
#include "sieve-objects.h"
#include "sieve-extensions.h"
#include "sieve-execute.h"

/*
 * Action execution environment
 */

struct sieve_action_exec_env {
	const struct sieve_execute_env *exec_env;
	const struct sieve_action *action;
	struct event *event;

	struct sieve_result *result;
	struct sieve_error_handler *ehandler;

	struct sieve_message_context *msgctx;
};

struct event_passthrough *
sieve_action_create_finish_event(const struct sieve_action_exec_env *aenv);

/*
 * Action flags
 */

enum sieve_action_flags {
	SIEVE_ACTFLAG_TRIES_DELIVER = (1 << 0),
	SIEVE_ACTFLAG_SENDS_RESPONSE = (1 << 1),
	SIEVE_ACTFLAG_MAIL_STORAGE = (1 << 2)
};

/*
 * Action definition
 */

struct sieve_action_def {
	const char *name;
	unsigned int flags;

	bool (*equals)(const struct sieve_script_env *senv,
		       const struct sieve_action *act1,
		       const struct sieve_action *act2);

	/* Result verification */
	int (*check_duplicate)(const struct sieve_runtime_env *renv,
			       const struct sieve_action *act,
			       const struct sieve_action *act_other);
	int (*check_conflict)(const struct sieve_runtime_env *renv,
			      const struct sieve_action *act,
			      const struct sieve_action *act_other);

	/* Result printing */
	void (*print)(const struct sieve_action *action,
		      const struct sieve_result_print_env *penv, bool *keep);

	/* Result execution */
	int (*start)(const struct sieve_action_exec_env *aenv,
		     void **tr_context);
	int (*execute)(const struct sieve_action_exec_env *aenv,
		       void *tr_context);
	int (*commit)(const struct sieve_action_exec_env *aenv,
		      void *tr_context, bool *keep);
	void (*rollback)(const struct sieve_action_exec_env *aenv,
			 void *tr_context, bool success);
	void (*finish)(const struct sieve_action_exec_env *aenv, bool last,
		       void *tr_context, int status);
};

/*
 * Action instance
 */

struct sieve_action {
	const struct sieve_action_def *def;
	const struct sieve_extension *ext;
	struct event *event;

	const char *name;
	const char *location;
	void *context;
	struct mail *mail;
	bool executed;
};

#define sieve_action_is(act, definition) ((act)->def == &(definition))
#define sieve_action_name(act) ((act)->name)

/*
 * Action side effects
 */

/* Side effect object */

struct sieve_side_effect_def {
	struct sieve_object_def obj_def;

	/* Precedence (side effects with higher value are executed first) */

	unsigned int precedence;

	/* The action it is supposed to link to */
	const struct sieve_action_def *to_action;

	/* Context coding */
	bool (*dump_context)(const struct sieve_side_effect *seffect,
			     const struct sieve_dumptime_env *renv,
			     sieve_size_t *address);
	int (*read_context)(const struct sieve_side_effect *seffect,
			    const struct sieve_runtime_env *renv,
			    sieve_size_t *address, void **se_context);

	/* Result verification */
	int (*merge)(const struct sieve_runtime_env *renv,
		     const struct sieve_action *action,
		     const struct sieve_side_effect *old_seffect,
		     const struct sieve_side_effect *new_seffect,
		     void **old_context);

	/* Result printing */
	void (*print)(const struct sieve_side_effect *seffect,
		      const struct sieve_action *action,
		      const struct sieve_result_print_env *penv, bool *keep);

	/* Result execution */

	int (*pre_execute)(const struct sieve_side_effect *seffect,
			   const struct sieve_action_exec_env *aenv,
			   void **context, void *tr_context);
	int (*post_execute)(const struct sieve_side_effect *seffect,
			    const struct sieve_action_exec_env *aenv,
			    void *tr_context);
	void (*post_commit)(const struct sieve_side_effect *seffect,
			    const struct sieve_action_exec_env *aenv,
			    void *tr_context, bool *keep);
	void (*rollback)(const struct sieve_side_effect *seffect,
			 const struct sieve_action_exec_env *aenv,
			 void *tr_context, bool success);
};

struct sieve_side_effect {
	struct sieve_object object;

	const struct sieve_side_effect_def *def;

	void *context;
};

/*
 * Side effect operand
 */

#define SIEVE_EXT_DEFINE_SIDE_EFFECT(SEF) SIEVE_EXT_DEFINE_OBJECT(SEF)
#define SIEVE_EXT_DEFINE_SIDE_EFFECTS(SEFS) SIEVE_EXT_DEFINE_OBJECTS(SEFS)

#define SIEVE_OPT_SIDE_EFFECT (-1)

extern const struct sieve_operand_class sieve_side_effect_operand_class;

static inline void
sieve_opr_side_effect_emit(struct sieve_binary_block *sblock,
			   const struct sieve_extension *ext,
			   const struct sieve_side_effect_def *seff)
{
	sieve_opr_object_emit(sblock, ext, &seff->obj_def);
}

bool sieve_opr_side_effect_dump(const struct sieve_dumptime_env *denv,
				sieve_size_t *address);
int sieve_opr_side_effect_read(const struct sieve_runtime_env *renv,
			       sieve_size_t *address,
			       struct sieve_side_effect *seffect);

/*
 * Optional operands
 */

int sieve_action_opr_optional_dump(const struct sieve_dumptime_env *denv,
				   sieve_size_t *address, signed int *opt_code);

int sieve_action_opr_optional_read(const struct sieve_runtime_env *renv,
				   sieve_size_t *address, signed int *opt_code,
				   int *exec_status,
				   struct sieve_side_effects_list **list);

/*
 * Core actions
 */

extern const struct sieve_action_def act_redirect;
extern const struct sieve_action_def act_store;
extern const struct sieve_action_def act_discard;

/*
 * Store action
 */

struct act_store_context {
	/* Folder name represented in utf-8 */
	const char *mailbox;
};

struct act_store_transaction {
	struct act_store_context *context;
	struct mailbox *box;
	struct mailbox_transaction_context *mail_trans;

	const char *mailbox_name;
	const char *mailbox_identifier;

	const char *error;
	enum mail_error error_code;

	enum mail_flags flags;
	ARRAY_TYPE(const_string) keywords;

	bool flags_altered:1;
	bool disabled:1;
	bool redundant:1;
};

int sieve_act_store_add_to_result(const struct sieve_runtime_env *renv,
				  const char *name,
				  struct sieve_side_effects_list *seffects,
				  const char *folder);

void sieve_act_store_add_flags(const struct sieve_action_exec_env *aenv,
			       void *tr_context, const char *const *keywords,
			       enum mail_flags flags);

void sieve_act_store_get_storage_error(const struct sieve_action_exec_env *aenv,
				       struct act_store_transaction *trans);

/*
 * Redirect action
 */

struct act_redirect_context {
	const struct smtp_address *to_address;
};

int sieve_act_redirect_add_to_result(const struct sieve_runtime_env *renv,
				     const char *name,
				     struct sieve_side_effects_list *seffects,
				     const struct smtp_address *to_address);

/*
 * Action utility functions
 */

/* Checking for duplicates */

bool sieve_action_duplicate_check_available(
	const struct sieve_script_env *senv);
bool sieve_action_duplicate_check(const struct sieve_script_env *senv,
				  const void *id, size_t id_size);
void sieve_action_duplicate_mark(const struct sieve_script_env *senv,
				 const void *id, size_t id_size, time_t time);
void sieve_action_duplicate_flush(const struct sieve_script_env *senv);

/* Rejecting mail */

int sieve_action_reject_mail(const struct sieve_action_exec_env *aenv,
			     const struct smtp_address *recipient,
			     const char *reason);

/*
 * Mailbox
 */

// FIXME: move this to a more appropriate location
bool sieve_mailbox_check_name(const char *mailbox, const char **error_r);

#endif
