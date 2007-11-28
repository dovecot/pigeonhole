#include "lib.h"
#include "str-sanitize.h"
#include "mail-storage.h"
#include "mail-namespace.h"

#include "sieve-extensions.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"
#include "sieve-actions.h"\

/* 
 * Side-effects 'extension' 
 */

static int ext_my_id = -1;

static bool seffect_extension_load(int ext_id);
static bool seffect_binary_load(struct sieve_binary *sbin);

const struct sieve_extension side_effects_extension = {
	"@side-effects",
	seffect_extension_load,
	NULL, NULL, 
	seffect_binary_load,
	NULL,
	SIEVE_EXT_DEFINE_NO_OPCODES,
	NULL
};
	
static bool seffect_extension_load(int ext_id) 
{
	ext_my_id = ext_id;
	return TRUE;
}

/*
 * Binary context
 */

static inline const struct sieve_side_effect_extension *
	sieve_side_effect_extension_get(struct sieve_binary *sbin, int ext_id)
{
	return (const struct sieve_side_effect_extension *)
		sieve_binary_registry_get_object(sbin, ext_my_id, ext_id);
}

void sieve_side_effect_extension_set
	(struct sieve_binary *sbin, int ext_id,
		const struct sieve_side_effect_extension *ext)
{
	sieve_binary_registry_set_object
		(sbin, ext_my_id, ext_id, (const void *) ext);
}

static bool seffect_binary_load(struct sieve_binary *sbin)
{
	sieve_binary_registry_init(sbin, ext_my_id);
	
	return TRUE;
}

/*
 * Actions common to multiple core commands 
 */
 
/* Store action */

static bool act_store_check_duplicate
	(const struct sieve_runtime_env *renv ATTR_UNUSED,
		const struct sieve_action *action1 ATTR_UNUSED, 
		void *context1, void *context2);
static void act_store_print
	(const struct sieve_action *action ATTR_UNUSED, void *context);

static bool act_store_start
	(const struct sieve_action *action,
		const struct sieve_action_exec_env *aenv, void *context, void **tr_context);
static bool act_store_execute
	(const struct sieve_action *action, 
		const struct sieve_action_exec_env *aenv, void *tr_context);
static bool act_store_commit
	(const struct sieve_action *action, 
		const struct sieve_action_exec_env *aenv, void *tr_context);
static void act_store_rollback
	(const struct sieve_action *action, 
		const struct sieve_action_exec_env *aenv, void *tr_context, bool success);
		
const struct sieve_action act_store = {
	"store",
	act_store_check_duplicate, 
	NULL, 
	act_store_print,
	act_store_start,
	act_store_execute,
	act_store_commit,
	act_store_rollback,
};

bool sieve_act_store_add_to_result
(const struct sieve_runtime_env *renv, const char *folder)
{
	pool_t pool;
	struct act_store_context *act;
	
	/* Add redirect action to the result */
	pool = sieve_result_pool(renv->result);
	act = p_new(pool, struct act_store_context, 1);
	act->folder = p_strdup(pool, folder);

	return sieve_result_add_action(renv, &act_store, (void *) act);
}

/* Store action implementation */

static bool act_store_check_duplicate
(const struct sieve_runtime_env *renv ATTR_UNUSED,
	const struct sieve_action *action1 ATTR_UNUSED, 
	void *context1, void *context2)
{
	struct act_store_context *ctx1 = (struct act_store_context *) context1;
	struct act_store_context *ctx2 = (struct act_store_context *) context2;
	
	if ( strcmp(ctx1->folder, ctx2->folder) == 0 ) 
		return TRUE;
		
	return FALSE;
}

static void act_store_print
(const struct sieve_action *action ATTR_UNUSED, void *context)	
{
	struct act_store_context *ctx = (struct act_store_context *) context;
	
	printf("* store message in folder: %s\n", ctx->folder);
}

/* Store transaction */

static void act_store_get_storage_error
(const struct sieve_action_exec_env *aenv, struct act_store_transaction *trans)
{
	enum mail_error error;
	pool_t pool = sieve_result_pool(aenv->result);
	
	trans->error = p_strdup(pool, 
		mail_storage_get_last_error(trans->namespace->storage, &error));
}

static bool act_store_start
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv, void *context, void **tr_context)
{  
	struct act_store_context *ctx = (struct act_store_context *) context;
	struct act_store_transaction *trans;
	struct mail_namespace *ns;
	struct mailbox *box;
	pool_t pool;

	ns = mail_namespace_find(aenv->mailenv->namespaces, &ctx->folder);
	if (ns == NULL) 
		return FALSE;
		
	box = mailbox_open(ns->storage, ctx->folder, NULL, MAILBOX_OPEN_FAST |
		MAILBOX_OPEN_KEEP_RECENT);
						
	pool = sieve_result_pool(aenv->result);
	trans = p_new(pool, struct act_store_transaction, 1);
	trans->context = ctx;
	trans->namespace = ns;
	trans->box = box;
	
	if ( box == NULL ) {
		printf("Open failed\n");
		act_store_get_storage_error(aenv, trans);
	}	
	
	*tr_context = (void *)trans;

	return TRUE;
}

static bool act_store_execute
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv, void *tr_context)
{   
	struct act_store_transaction *trans = 
		(struct act_store_transaction *) tr_context;
			
	if ( trans->box == NULL ) return FALSE;
	
	trans->mail_trans = mailbox_transaction_begin
		(trans->box, MAILBOX_TRANSACTION_FLAG_EXTERNAL);

  if (mailbox_copy(trans->mail_trans, aenv->msgdata->mail, 0, NULL, NULL) < 0) {
  	printf("Copy failed\n");
  	act_store_get_storage_error(aenv, trans);
 		return FALSE;
 	}
 		 	
	return TRUE;
}

static void act_store_log_status
(struct act_store_transaction *trans, 
	const struct sieve_message_data *msgdata, bool rolled_back, bool status )
{
	const char *msgid, *mailbox_name;
	
	if (mail_get_first_header(msgdata->mail, "Message-ID", &msgid) <= 0)
		msgid = "";
	else
		msgid = str_sanitize(msgid, 80);

	if ( trans->box == NULL )
		mailbox_name = str_sanitize(trans->context->folder, 80);
	else
		mailbox_name = str_sanitize(mailbox_get_name(trans->box), 80);

	if (!rolled_back && status) {
		i_info("msgid=%s: saved mail to %s", msgid, mailbox_name);
	} else {
		const char *errstr;
		enum mail_error error;
		
		if ( trans->error != NULL )
			errstr = trans->error;
		else
			errstr = mail_storage_get_last_error(trans->namespace->storage, &error);

		if ( status )
			i_info("msgid=%s: save to %s aborted.", msgid, mailbox_name);
		else
			i_info("msgid=%s: save failed to %s: %s", msgid, mailbox_name, errstr);
	}
}

static bool act_store_commit
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv, void *tr_context)
{  
	struct act_store_transaction *trans = 
		(struct act_store_transaction *) tr_context;
	bool status = mailbox_transaction_commit(&trans->mail_trans) == 0;
	
	act_store_log_status(trans, aenv->msgdata, FALSE, status);
	
	if ( trans->box != NULL )
		mailbox_close(&trans->box);
	
	return status;
}

static void act_store_rollback
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv, void *tr_context, bool success)
{
	struct act_store_transaction *trans = 
		(struct act_store_transaction *) tr_context;

	if ( trans->mail_trans != NULL )
	  mailbox_transaction_rollback(&trans->mail_trans);
  
  act_store_log_status(trans, aenv->msgdata, TRUE, success);

	if ( trans->box != NULL )  
	  mailbox_close(&trans->box);
}




