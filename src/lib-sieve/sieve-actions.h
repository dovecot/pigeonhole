#ifndef __SIEVE_ACTIONS_H
#define __SIEVE_ACTIONS_H

#include "lib.h"
#include "mail-storage.h"

#include "sieve-common.h"

/* Sieve action */

struct sieve_action_exec_env { 
	struct sieve_result *result;
	const struct sieve_message_data *msgdata;
	const struct sieve_mail_environment *mailenv;
};

struct sieve_action {
	const char *name;

	bool (*check_duplicate)	
		(const struct sieve_runtime_env *renv,
			const struct sieve_action *action, void *context1, void *context2);	
	bool (*check_conflict)
		(const struct sieve_runtime_env *renv,
			const struct sieve_action *action1, const struct sieve_action *action2,
			void *context1);

	void (*print)
		(const struct sieve_action *action, void *context);	
		
	bool (*start)
		(const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *context, 
			void **tr_context);		
	bool (*execute)
		(const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *tr_context);
	bool (*commit)
		(const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *tr_context);
	void (*rollback)
		(const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *tr_context, bool success);
};

/* Action side effects */

struct sieve_side_effect_extension;

struct sieve_side_effect {
	const char *name;
	const struct sieve_action *to_action;
	
	const struct sieve_side_effect_extension *extension;
	unsigned int ext_code;

	bool (*pre_execute)
		(const struct sieve_side_effect *seffect, const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void **se_context, 
			void *tr_context);
	bool (*post_execute)
		(const struct sieve_side_effect *seffect, const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *se_context, 
			void *tr_context);
	bool (*post_commit)
		(const struct sieve_side_effect *seffect, const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *se_context,
			void *tr_context);
	void (*rollback)
		(const struct sieve_side_effect *seffect, const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *se_context,
			void *tr_context, bool success);
};

struct sieve_side_effect_extension {
	const struct sieve_extension *extension;

	/* Extension can introduce a single or multiple action side effects */
	union {
		const struct sieve_side_effect **list;
		const struct sieve_side_effect *single;
	} side_effects;
	unsigned int side_effects_count;
};

#define SIEVE_EXT_DEFINE_SIDE_EFFECT(SEF) SIEVE_EXT_DEFINE_OBJECT(SEF)
#define SIEVE_EXT_DEFINE_SIDE_EFFECTS(SEFS) SIEVE_EXT_DEFINE_OBJECTS(SEFS)

void sieve_side_effect_extension_set
	(struct sieve_binary *sbin, int ext_id,
		const struct sieve_side_effect_extension *ext);

void sieve_opr_side_effect_emit
	(struct sieve_binary *sbin, const struct sieve_side_effect *seffect, 
		int ext_id);

/* Actions common to multiple commands */

const struct sieve_action act_store;

struct act_store_context {
	const char *folder;
};

struct act_store_transaction {
	struct act_store_context *context;
	struct mail_namespace *namespace;
	struct mailbox *box;
	struct mailbox_transaction_context *mail_trans;
	const char *error;
};

bool sieve_act_store_add_to_result
	(const struct sieve_runtime_env *renv, const char *folder);

		
#endif /* __SIEVE_ACTIONS_H */
