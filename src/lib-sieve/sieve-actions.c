#include "lib.h"
#include "ioloop.h"
#include "str-sanitize.h"
#include "mail-storage.h"
#include "mail-namespace.h"

#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"
#include "sieve-dump.h"
#include "sieve-result.h"
#include "sieve-actions.h"

/*
 * Message transmission (FIXME: place this somewhere more appropriate)
 */
 
const char *sieve_get_new_message_id
	(const struct sieve_script_env *senv)
{
	static int count = 0;
	
	return t_strdup_printf("<dovecot-sieve-%s-%s-%d@%s>",
		dec2str(ioloop_timeval.tv_sec), dec2str(ioloop_timeval.tv_usec),
    count++, senv->hostname);
}

/*
 * Side-effect operand
 */
 
const struct sieve_operand_class sieve_side_effect_operand_class = 
	{ "SIDE-EFFECT" };

bool sieve_opr_side_effect_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	const struct sieve_object *obj;
	const struct sieve_side_effect *seffect;
	
	if ( !sieve_opr_object_dump
		(denv, &sieve_side_effect_operand_class, address, &obj) )
		return FALSE;
	
	seffect = (const struct sieve_side_effect *) obj;

	if ( seffect->dump_context != NULL ) {
		sieve_code_descend(denv);
		if ( !seffect->dump_context(seffect, denv, address) ) {
			return FALSE;	
		}
		sieve_code_ascend(denv);
	}

	return TRUE;
}

/*
 * Actions common to multiple core commands 
 */
 
/* Store action */

static int act_store_check_duplicate
	(const struct sieve_runtime_env *renv, const struct sieve_action *action1, 
		void *context1, void *context2, 
		const char *location1, const char *location2);
static void act_store_print
	(const struct sieve_action *action, const struct sieve_result_print_env *rpenv,
		void *context, bool *keep);

static bool act_store_start
	(const struct sieve_action *action,
		const struct sieve_action_exec_env *aenv, void *context, void **tr_context);
static bool act_store_execute
	(const struct sieve_action *action, 
		const struct sieve_action_exec_env *aenv, void *tr_context);
static bool act_store_commit
	(const struct sieve_action *action, 
		const struct sieve_action_exec_env *aenv, void *tr_context, bool *keep);
static void act_store_rollback
	(const struct sieve_action *action, 
		const struct sieve_action_exec_env *aenv, void *tr_context, bool success);
		
const struct sieve_action act_store = {
	"store",
	SIEVE_ACTFLAG_TRIES_DELIVER,
	act_store_check_duplicate, 
	NULL, 
	act_store_print,
	act_store_start,
	act_store_execute,
	act_store_commit,
	act_store_rollback,
};

int sieve_act_store_add_to_result
(const struct sieve_runtime_env *renv, 
	struct sieve_side_effects_list *seffects, const char *folder,
	unsigned int source_line)
{
	pool_t pool;
	struct act_store_context *act;
	
	/* Add redirect action to the result */
	pool = sieve_result_pool(renv->result);
	act = p_new(pool, struct act_store_context, 1);
	act->folder = p_strdup(pool, folder);

	return sieve_result_add_action(renv, &act_store, seffects, 
		source_line, (void *) act);
}

/* Store action implementation */

static int act_store_check_duplicate
(const struct sieve_runtime_env *renv ATTR_UNUSED,
	const struct sieve_action *action1 ATTR_UNUSED, 
	void *context1, void *context2,
	const char *location1 ATTR_UNUSED, const char *location2 ATTR_UNUSED)
{
	struct act_store_context *ctx1 = (struct act_store_context *) context1;
	struct act_store_context *ctx2 = (struct act_store_context *) context2;
	
	if ( strcmp(ctx1->folder, ctx2->folder) == 0 ) 
		return 1;
		
	return ( 
		strcasecmp(ctx1->folder, "INBOX") == 0 && 
		strcasecmp(ctx2->folder, "INBOX") == 0 
	); 
}

static void act_store_print
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_result_print_env *rpenv, void *context, bool *keep)	
{
	struct act_store_context *ctx = (struct act_store_context *) context;
	
	sieve_result_action_printf(rpenv, "store message in folder: %s", ctx->folder);
	
	*keep = FALSE;
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
	struct mail_namespace *ns = NULL;
	struct mailbox *box = NULL;
	pool_t pool;

	if ( aenv->scriptenv->namespaces != NULL ) {
		ns = mail_namespace_find(aenv->scriptenv->namespaces, &ctx->folder);
		if (ns == NULL) 
			return FALSE;
		
		box = mailbox_open(ns->storage, ctx->folder, NULL, MAILBOX_OPEN_FAST |
			MAILBOX_OPEN_KEEP_RECENT);
	
		if (box != NULL && mailbox_sync(box, 0, 0, NULL) < 0) {
			mailbox_close(&box);
			box = NULL;
		}
	}
				
	pool = sieve_result_pool(aenv->result);
	trans = p_new(pool, struct act_store_transaction, 1);
	trans->context = ctx;
	trans->namespace = ns;
	trans->box = box;
	trans->flags = 0;
		
	if ( ns != NULL && box == NULL ) 
		act_store_get_storage_error(aenv, trans);	
	
	*tr_context = (void *)trans;

	return TRUE;
}

static bool act_store_execute
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv, void *tr_context)
{   
	struct act_store_transaction *trans = 
		(struct act_store_transaction *) tr_context;
	struct mail_keywords *keywords = NULL;
	
	if ( trans == NULL || trans->namespace == NULL || trans->box == NULL ) 
		return FALSE;
	
	trans->mail_trans = mailbox_transaction_begin
		(trans->box, MAILBOX_TRANSACTION_FLAG_EXTERNAL);

	trans->dest_mail = mail_alloc(trans->mail_trans, 0, NULL);

	if ( array_is_created(&trans->keywords) && array_count(&trans->keywords) > 0 ) 
	{
		const char *const *kwds;
		
		(void)array_append_space(&trans->keywords);
		kwds = array_idx(&trans->keywords, 0);
				
		/* FIXME: Do we need to clear duplicates? */
		
		if ( mailbox_keywords_create(trans->box, kwds, &keywords) < 0) {
			sieve_result_error(aenv, "invalid keywords set for stored message");
			keywords = NULL;
		}
	}
	
	if (mailbox_copy(trans->mail_trans, aenv->msgdata->mail, trans->flags, 
		keywords, trans->dest_mail) < 0) {
		act_store_get_storage_error(aenv, trans);
 		return FALSE;
 	}
 		 	
	return TRUE;
}

static void act_store_log_status
(struct act_store_transaction *trans, 
	const struct sieve_action_exec_env *aenv, bool rolled_back, bool status )
{
	const char *mailbox_name;
	
	if ( trans->box == NULL )
		mailbox_name = str_sanitize(trans->context->folder, 80);
	else
		mailbox_name = str_sanitize(mailbox_get_name(trans->box), 80);

	if ( trans->namespace == NULL ) {
		sieve_result_log(aenv, "store into mailbox '%s' not performed", mailbox_name);
	} else {	
		if ( !rolled_back && status ) {
			sieve_result_log(aenv, "stored mail into mailbox '%s'", mailbox_name);
		} else {
			const char *errstr;
			enum mail_error error;
		
			if ( trans->error != NULL )
				errstr = trans->error;
			else
				errstr = mail_storage_get_last_error(trans->namespace->storage, &error);
			
			if ( status )
				sieve_result_log(aenv, "store into mailbox '%s' aborted", mailbox_name);
			else
				sieve_result_error(aenv, "failed to store into mailbox '%s': %s", 
					mailbox_name, errstr);
		}
	}
}

static bool act_store_commit
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv, void *tr_context, bool *keep)
{  
	struct act_store_transaction *trans = 
		(struct act_store_transaction *) tr_context;
	bool status = TRUE;

	if ( trans->namespace != NULL ) {
		if ( trans->dest_mail != NULL ) 
			mail_free(&trans->dest_mail);	

		status = mailbox_transaction_commit(&trans->mail_trans) == 0;
	} 
	
	act_store_log_status(trans, aenv, FALSE, status);
	*keep = !status;
		
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

	act_store_log_status(trans, aenv, TRUE, success);

	if ( trans->dest_mail != NULL ) 
		mail_free(&trans->dest_mail);	

	if ( trans->mail_trans != NULL )
	  mailbox_transaction_rollback(&trans->mail_trans);
  
	if ( trans->box != NULL )  
	  mailbox_close(&trans->box);
}




