/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file 
 */

#include "lib.h"
#include "strfuncs.h"
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
 * Action execution environment
 */

const char *sieve_action_get_location(const struct sieve_action_exec_env *aenv)
{
	return t_strdup_printf("msgid=%s", aenv->msgdata->id == NULL ?
		"unspecified" : str_sanitize(aenv->msgdata->id, 80));
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
 * Store action
 */
 
/* Forward declarations */

static bool act_store_equals
	(const struct sieve_script_env *senv, const void *ctx1, const void *ctx2);
	
static int act_store_check_duplicate
	(const struct sieve_runtime_env *renv, 
		const struct sieve_action_data *act, 
		const struct sieve_action_data *act_other);
static void act_store_print
	(const struct sieve_action *action, 
		const struct sieve_result_print_env *rpenv, void *context, bool *keep);

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
		
/* Action object */

const struct sieve_action act_store = {
	"store",
	SIEVE_ACTFLAG_TRIES_DELIVER,
	act_store_equals,
	act_store_check_duplicate, 
	NULL, 
	act_store_print,
	act_store_start,
	act_store_execute,
	act_store_commit,
	act_store_rollback,
};

/* API */

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
		source_line, (void *) act, 0);
}

/* Equality */

static bool act_store_equals
(const struct sieve_script_env *senv, const void *ctx1, const void *ctx2)
{
	struct act_store_context *st_ctx1 = (struct act_store_context *) ctx1;
	struct act_store_context *st_ctx2 = (struct act_store_context *) ctx2;
	const char *folder1, *folder2;
	
	if ( st_ctx1 == NULL && st_ctx2 == NULL )
		return TRUE;
		
	folder1 = ( st_ctx1 == NULL ? 
		SIEVE_SCRIPT_DEFAULT_MAILBOX(senv) : st_ctx1->folder );
	folder2 = ( st_ctx2 == NULL ? 
		SIEVE_SCRIPT_DEFAULT_MAILBOX(senv) : st_ctx2->folder );
	
	if ( strcmp(folder1, folder2) == 0 ) 
		return TRUE;
		
	return 
		( strcasecmp(folder1, "INBOX") == 0 && strcasecmp(folder2, "INBOX") == 0 ); 

}

/* Result verification */

static int act_store_check_duplicate
(const struct sieve_runtime_env *renv,
	const struct sieve_action_data *act, 
	const struct sieve_action_data *act_other)
{
	return ( act_store_equals(renv->scriptenv, act->context, act_other->context)
		? 1 : 0 );
}

/* Result printing */

static void act_store_print
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_result_print_env *rpenv, void *context, bool *keep)	
{
	struct act_store_context *ctx = (struct act_store_context *) context;
	const char *folder;

	folder = ( ctx == NULL ? 
		SIEVE_SCRIPT_DEFAULT_MAILBOX(rpenv->scriptenv) : ctx->folder );	

	sieve_result_action_printf(rpenv, "store message in folder: %s", 
		str_sanitize(folder, 128));
	
	*keep = FALSE;
}

/* Action implementation */

static void act_store_get_storage_error
(const struct sieve_action_exec_env *aenv, struct act_store_transaction *trans)
{
	enum mail_error error;
	pool_t pool = sieve_result_pool(aenv->result);
	
	trans->error = p_strdup(pool, 
		mail_storage_get_last_error(trans->namespace->storage, &error));
}

static struct mailbox *act_store_mailbox_open
(const struct sieve_action_exec_env *aenv, const char *name,
	struct mail_namespace **ns_r)
{
	struct mail_storage **storage = &(aenv->exec_status->last_storage);
	enum mailbox_flags flags =
		MAILBOX_FLAG_KEEP_RECENT | MAILBOX_FLAG_SAVEONLY |
		MAILBOX_FLAG_POST_SESSION;
	struct mailbox *box;
	enum mail_error error;
	const char *folder = name;

	if (strcasecmp(folder, "INBOX") == 0) {
		/* Deliveries to INBOX must always succeed, regardless of ACLs */
		flags |= MAILBOX_FLAG_IGNORE_ACLS;
	}

	*ns_r = mail_namespace_find(aenv->scriptenv->namespaces, &folder);
	if ( *ns_r == NULL) {
		*storage = NULL;
		return NULL;
	}
	
	if ( *folder == '\0' ) {
		/* delivering to a namespace prefix means we actually want to
		 * deliver to the INBOX instead
		 */
		folder = "INBOX";
		flags |= MAILBOX_FLAG_IGNORE_ACLS;

		*ns_r = mail_namespace_find(aenv->scriptenv->namespaces, &folder);
    	if ( *ns_r == NULL) {
        	*storage = NULL;
	        return NULL;
    	}
	}

	/* First attempt at opening the box */
	box = mailbox_alloc((*ns_r)->list, folder, NULL, flags);
	if ( mailbox_open(box) == 0 ) {
		/* Success */
		return box;
	}

	/* Failed */

	*storage = mailbox_get_storage(box);
	(void)mail_storage_get_last_error(*storage, &error);
	
	/* Only continue when the mailbox is missing and when we are allowed to
	 * create it.
	 */
	if ( !aenv->scriptenv->mailbox_autocreate || error != MAIL_ERROR_NOTFOUND ) {
		mailbox_close(&box);
		return NULL;
	}

	/* Try creating it. */
    if ( mailbox_create(box, NULL, FALSE) < 0 ) {
        (void)mail_storage_get_last_error(*storage, &error);
        mailbox_close(&box);
        return NULL;
    }

	/* Subscribe to it if required */
	if ( aenv->scriptenv->mailbox_autosubscribe ) {
		(void)mailbox_list_set_subscribed((*ns_r)->list, folder, TRUE);
	}

	/* Try opening again */
	if ( mailbox_open(box) < 0 || mailbox_sync(box, 0, 0, NULL) < 0 ) {
		/* Failed definitively */
		mailbox_close(&box);
		return NULL;
	}

	return box;
}

static bool act_store_start
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv, void *context, void **tr_context)
{  
	struct act_store_context *ctx = (struct act_store_context *) context;
	struct act_store_transaction *trans;
	struct mail_namespace *ns = NULL;
	struct mailbox *box = NULL;
	pool_t pool = sieve_result_pool(aenv->result);

	/* If context is NULL, the store action is the result of (implicit) keep */	
	if ( ctx == NULL ) {
		ctx = p_new(pool, struct act_store_context, 1);
		ctx->folder = p_strdup(pool, SIEVE_SCRIPT_DEFAULT_MAILBOX(aenv->scriptenv));
	}

	/* Open the requested mailbox */

	/* NOTE: The caller of the sieve library is allowed to leave namespaces set 
	 * to NULL. This implementation will then skip actually storing the message.
	 */
	if ( aenv->scriptenv->namespaces != NULL ) {
		box = act_store_mailbox_open(aenv, ctx->folder, &ns);
	}
				
	/* Create transaction context */
	trans = p_new(pool, struct act_store_transaction, 1);
	trans->context = ctx;
	trans->namespace = ns;
	trans->box = box;
	trans->flags = 0;
	
	*tr_context = (void *)trans;

	return ( (aenv->scriptenv->namespaces == NULL) || (box != NULL) );
}

static bool act_store_execute
(const struct sieve_action *action ATTR_UNUSED, 
	const struct sieve_action_exec_env *aenv, void *tr_context)
{   
	struct act_store_transaction *trans = 
		(struct act_store_transaction *) tr_context;
	struct mail_keywords *keywords = NULL;
	struct mail_save_context *save_ctx;
	
	/* Verify transaction */
	if ( trans == NULL ) return FALSE;

	/* Exit early if namespace is not available */
	if ( trans->namespace == NULL ) {
		if ( aenv->scriptenv->namespaces == NULL )
			return TRUE;

		return FALSE;
	} else if ( trans->box == NULL ) return FALSE;

	/* Mark attempt to store in default mailbox */
	if ( strcmp(trans->context->folder, 
		SIEVE_SCRIPT_DEFAULT_MAILBOX(aenv->scriptenv)) == 0 ) 
		aenv->exec_status->tried_default_save = TRUE;

	/* Mark attempt to use storage. Can only get here when all previous actions
	 * succeeded. 
	 */
	aenv->exec_status->last_storage = mailbox_get_storage(trans->box);
	
	/* Start mail transaction */
	trans->mail_trans = mailbox_transaction_begin
		(trans->box, MAILBOX_TRANSACTION_FLAG_EXTERNAL);

	/* Create mail object for stored message */
	trans->dest_mail = mail_alloc(trans->mail_trans, 0, NULL);

	/* Collect keywords added by side-effects */
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
	
	/* Store the message */
	save_ctx = mailbox_save_alloc(trans->mail_trans);
	mailbox_save_set_flags(save_ctx, trans->flags, keywords);
	mailbox_save_set_dest_mail(save_ctx, trans->dest_mail);
	if ( mailbox_copy(&save_ctx, aenv->msgdata->mail) < 0 ) {
		act_store_get_storage_error(aenv, trans);
 		return FALSE;
 	}
 	
 	if ( keywords != NULL ) {
 		mailbox_keywords_unref(trans->box, &keywords);
 	}
 		 	
	return TRUE;
}

static void act_store_log_status
(struct act_store_transaction *trans, 
	const struct sieve_action_exec_env *aenv, bool rolled_back, bool status )
{
	const char *mailbox_name;
	
	mailbox_name = str_sanitize(trans->context->folder, 128);

	if ( trans->namespace == NULL ) {
		if ( aenv->scriptenv->namespaces == NULL )
			sieve_result_log(aenv, "store into mailbox '%s' skipped", mailbox_name);
		else
			sieve_result_error
				(aenv, "failed to find namespace for mailbox '%s'", mailbox_name);
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

	/* Verify transaction */
	if ( trans == NULL ) return FALSE;

	/* Exit early if namespace is not available */
	if ( trans->namespace == NULL ) {
		if ( aenv->scriptenv->namespaces == NULL ) {
			act_store_log_status(trans, aenv, FALSE, status);
			*keep = FALSE;
			return TRUE;
		}

		return FALSE;
	} else if ( trans->box == NULL ) return FALSE;

	/* Mark attempt to use storage. Can only get here when all previous actions
	 * succeeded. 
	 */
	aenv->exec_status->last_storage = trans->namespace->storage;

	/* Free mail object for stored message */
	if ( trans->dest_mail != NULL ) 
		mail_free(&trans->dest_mail);	

	/* Commit mailbox transaction */	
	status = mailbox_transaction_commit(&trans->mail_trans) == 0;

	/* Note the fact that the message was stored at least once */
	if ( status )
		aenv->exec_status->message_saved = TRUE;
	
	/* Log our status */
	act_store_log_status(trans, aenv, FALSE, status);
	
	/* Cancel implicit keep if all went well */
	*keep = !status;
	
	/* Close mailbox */	
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

	/* Log status */
	act_store_log_status(trans, aenv, TRUE, success);

	/* Free mailobject for stored message */
	if ( trans->dest_mail != NULL ) 
		mail_free(&trans->dest_mail);	

	/* Rollback mailbox transaction */
	if ( trans->mail_trans != NULL )
		mailbox_transaction_rollback(&trans->mail_trans);
  
	/* Close the mailbox */
	if ( trans->box != NULL )  
		mailbox_close(&trans->box);
}

/*
 * Action utility functions
 */

bool sieve_action_duplicate_check_available
(const struct sieve_script_env *senv)
{
	return ( senv->duplicate_check != NULL && senv->duplicate_mark != NULL );
}

int sieve_action_duplicate_check
(const struct sieve_script_env *senv, const void *id, size_t id_size)
{
	if ( senv->duplicate_check == NULL || senv->duplicate_mark == NULL)
		return 0;

	return senv->duplicate_check
		(id, id_size, senv->username); 
}

void sieve_action_duplicate_mark
(const struct sieve_script_env *senv, const void *id, size_t id_size,
	time_t time)
{
	if ( senv->duplicate_check == NULL || senv->duplicate_mark == NULL)
		return;

	senv->duplicate_mark
		(id, id_size, senv->username, time);
}
	

