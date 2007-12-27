#ifndef __SIEVE_ACTIONS_H
#define __SIEVE_ACTIONS_H

#include "lib.h"
#include "mail-storage.h"

#include "sieve-common.h"
#include "sieve-extensions-private.h"

/* Sieve action */

struct sieve_action_exec_env { 
	struct sieve_result *result;
	const struct sieve_message_data *msgdata;
	const struct sieve_script_env *scriptenv;
};

enum sieve_action_flags {
	SIEVE_ACTFLAG_TRIES_DELIVER = (1 << 0),
	SIEVE_ACTFLAG_SENDS_RESPONSE = (1 << 1)
};

struct sieve_action {
	const char *name;
	unsigned int flags;

	int (*check_duplicate)	
		(const struct sieve_runtime_env *renv,
			const struct sieve_action *action, void *context1, void *context2);	
	int (*check_conflict)
		(const struct sieve_runtime_env *renv,
			const struct sieve_action *action, 
			const struct sieve_action *other_action,
			void *context);

	void (*print)
		(const struct sieve_action *action, void *context, bool *keep);	
		
	bool (*start)
		(const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *context, 
			void **tr_context);		
	bool (*execute)
		(const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *tr_context);
	bool (*commit)
		(const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *tr_context, bool *keep);
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
	unsigned int code;
	
	bool (*dump_context)
		(const struct sieve_side_effect *seffect, 
			const struct sieve_dumptime_env *renv, sieve_size_t *address);
	bool (*read_context)
		(const struct sieve_side_effect *seffect, 
			const struct sieve_runtime_env *renv, sieve_size_t *address,
			void **se_context);
			
	void (*print)
		(const struct sieve_side_effect *seffect, const struct sieve_action *action, 
			void *se_context, bool *keep);

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
			void *tr_context, bool *keep);
	void (*rollback)
		(const struct sieve_side_effect *seffect, const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *se_context,
			void *tr_context, bool success);
};

struct sieve_side_effect_extension {
	const struct sieve_extension *extension;

	struct sieve_extension_obj_registry side_effects;
};

#define SIEVE_EXT_DEFINE_SIDE_EFFECT(SEF) SIEVE_EXT_DEFINE_OBJECT(SEF)
#define SIEVE_EXT_DEFINE_SIDE_EFFECTS(SEFS) SIEVE_EXT_DEFINE_OBJECTS(SEFS)

#define SIEVE_OPT_SIDE_EFFECT -1

void sieve_side_effect_extension_set
	(struct sieve_binary *sbin, int ext_id,
		const struct sieve_side_effect_extension *ext);

void sieve_opr_side_effect_emit
	(struct sieve_binary *sbin, const struct sieve_side_effect *seffect, 
		int ext_id);
bool sieve_opr_side_effect_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
const struct sieve_side_effect *sieve_opr_side_effect_read
	(struct sieve_binary *sbin, sieve_size_t *address);

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
	struct mail *dest_mail;
	const char *error;
};

int sieve_act_store_add_to_result
	(const struct sieve_runtime_env *renv, 
		struct sieve_side_effects_list *seffects, const char *folder);

/* Message transmission */

const char *sieve_get_new_message_id
	(const struct sieve_script_env *senv);

		
#endif /* __SIEVE_ACTIONS_H */
