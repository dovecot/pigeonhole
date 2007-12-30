#include "lib.h"
#include "ioloop.h"
#include "str-sanitize.h"
#include "mail-storage.h"
#include "mail-namespace.h"

#include "sieve-code.h"
#include "sieve-extensions-private.h"
#include "sieve-binary.h"
#include "sieve-interpreter.h"
#include "sieve-code-dumper.h"
#include "sieve-result.h"
#include "sieve-actions.h"

#include <ctype.h>

static struct sieve_extension_obj_registry seff_default_reg =
	SIEVE_EXT_DEFINE_NO_SIDE_EFFECTS;

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
	SIEVE_EXT_DEFINE_NO_OPERATIONS, /* Opcode is hardcoded */
	SIEVE_EXT_DEFINE_NO_OPERANDS
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
 * Side-effect operand
 */
 
static struct sieve_operand_class side_effect_class = 
	{ "side-effect", NULL };

struct sieve_operand side_effect_operand = { 
	"side-effect", 
	NULL, SIEVE_OPERAND_SIDE_EFFECT,
	&side_effect_class
};

void sieve_opr_side_effect_emit
	(struct sieve_binary *sbin, const struct sieve_side_effect *seffect, 
		int ext_id)
{ 
	(void) sieve_operand_emit_code(sbin, &side_effect_operand, -1);

	(void) sieve_extension_emit_obj
		(sbin, &seff_default_reg, seffect, side_effects, ext_id);
}

static const struct sieve_extension_obj_registry *
	sieve_side_effect_registry_get
(struct sieve_binary *sbin, unsigned int ext_index)
{
	int ext_id = -1; 
	const struct sieve_side_effect_extension *ext;
	
	if ( sieve_binary_extension_get_by_index(sbin, ext_index, &ext_id) == NULL )
		return NULL;

	if ( (ext=sieve_side_effect_extension_get(sbin, ext_id)) == NULL ) 
		return NULL;
		
	return &(ext->side_effects);
}

const struct sieve_side_effect *sieve_opr_side_effect_read
(struct sieve_binary *sbin, sieve_size_t *address)
{
	const struct sieve_operand *operand = sieve_operand_read(sbin, address);

	if ( operand == NULL || operand->class != &side_effect_class ) 
		return NULL;
		
	return sieve_extension_read_obj
		(struct sieve_side_effect, sbin, address, &seff_default_reg, 
			sieve_side_effect_registry_get);
}

bool sieve_opr_side_effect_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	const struct sieve_side_effect *seffect;

	sieve_code_mark(denv);
	seffect = sieve_opr_side_effect_read(denv->sbin, address);

	if ( seffect == NULL ) 
		return FALSE;

	sieve_code_dumpf(denv, "SIDE-EFFECT: %s", seffect->name);

	if ( seffect->dump_context != NULL ) {
		sieve_code_descend(denv);
		if ( !seffect->dump_context(seffect, denv, address) )
			return FALSE;	
		sieve_code_ascend(denv);
	}

	return TRUE;
}

/*
 * Actions common to multiple core commands 
 */
 
/* Store action */

static int act_store_check_duplicate
	(const struct sieve_runtime_env *renv ATTR_UNUSED,
		const struct sieve_action *action1 ATTR_UNUSED, 
		void *context1, void *context2);
static void act_store_print
	(const struct sieve_action *action ATTR_UNUSED, void *context, bool *keep);

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
	struct sieve_side_effects_list *seffects, const char *folder)
{
	pool_t pool;
	struct act_store_context *act;
	
	/* Add redirect action to the result */
	pool = sieve_result_pool(renv->result);
	act = p_new(pool, struct act_store_context, 1);
	act->folder = p_strdup(pool, folder);

	return sieve_result_add_action(renv, &act_store, seffects, (void *) act);
}

/* Store action implementation */

static int act_store_check_duplicate
(const struct sieve_runtime_env *renv ATTR_UNUSED,
	const struct sieve_action *action1 ATTR_UNUSED, 
	void *context1, void *context2)
{
	struct act_store_context *ctx1 = (struct act_store_context *) context1;
	struct act_store_context *ctx2 = (struct act_store_context *) context2;
	
	if ( strcmp(ctx1->folder, ctx2->folder) == 0 ) 
		return 1;
		
	return 0;
}

static void act_store_print
(const struct sieve_action *action ATTR_UNUSED, void *context, bool *keep)	
{
	struct act_store_context *ctx = (struct act_store_context *) context;
	
	printf("* store message in folder: %s\n", ctx->folder);
	
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
	}
					
	pool = sieve_result_pool(aenv->result);
	trans = p_new(pool, struct act_store_transaction, 1);
	trans->context = ctx;
	trans->namespace = ns;
	trans->box = box;
	
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

	if ( trans->namespace == NULL )
		return TRUE;
			
	if ( trans->box == NULL ) return FALSE;
	
	trans->mail_trans = mailbox_transaction_begin
		(trans->box, MAILBOX_TRANSACTION_FLAG_EXTERNAL);

	trans->dest_mail = mail_alloc(trans->mail_trans, 0, NULL);

	if (mailbox_copy(trans->mail_trans, aenv->msgdata->mail, 0, NULL, 
		trans->dest_mail) < 0) {
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
		sieve_result_log(aenv, "store into mailbox '%s' not performed.", mailbox_name);
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
				sieve_result_log(aenv, "store into mailbox '%s' aborted.", mailbox_name);
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




