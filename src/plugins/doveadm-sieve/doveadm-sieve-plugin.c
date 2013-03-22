/* Copyright (c) 2013 Dovecot authors, see the included COPYING file */

#include "lib.h"
#include "istream.h"
#include "sieve-storage.h"
#include "sieve-storage-list.h"
#include "sieve-storage-save.h"
#include "sieve-storage-script.h"
#include "mail-storage-private.h"

#define SIEVE_MAIL_CONTEXT(obj) \
	MODULE_CONTEXT(obj, sieve_storage_module)
#define SIEVE_USER_CONTEXT(obj) \
	MODULE_CONTEXT(obj, sieve_user_module)

#define MAILBOX_ATTRIBUTE_PREFIX_SIEVE \
	MAILBOX_ATTRIBUTE_PREFIX_DOVECOT_PVT"sieve/"
#define MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES \
	MAILBOX_ATTRIBUTE_PREFIX_SIEVE"files/"
#define MAILBOX_ATTRIBUTE_SIEVE_ACTIVE \
	MAILBOX_ATTRIBUTE_PREFIX_SIEVE"active"

struct sieve_mail_user {
	union mail_user_module_context module_ctx;

	struct sieve_instance *svinst;
	struct sieve_storage *sstorage;
};

struct sieve_mailbox_attribute_iter {
	struct mailbox_attribute_iter iter;
	struct mailbox_attribute_iter *super;

	struct sieve_list_context *sieve_list;
	string_t *name;

	bool failed;
	bool have_active;
};

void doveadm_sieve_plugin_init(struct module *module);
void doveadm_sieve_plugin_deinit(void);

static MODULE_CONTEXT_DEFINE_INIT(sieve_storage_module,
				  &mail_storage_module_register);
static MODULE_CONTEXT_DEFINE_INIT(sieve_user_module,
				  &mail_user_module_register);

const char *doveadm_sieve_plugin_version = DOVECOT_ABI_VERSION;

static const char *
mail_sieve_get_setting(void *context, const char *identifier)
{
	struct mail_user *mail_user = context;

	return mail_user_plugin_getenv(mail_user, identifier);
}

static const struct sieve_callbacks mail_sieve_callbacks = {
	NULL,
	mail_sieve_get_setting
};

static void mail_sieve_user_deinit(struct mail_user *user)
{
	struct sieve_mail_user *suser = SIEVE_USER_CONTEXT(user);

	sieve_storage_free(suser->sstorage);
	sieve_deinit(&suser->svinst);

	suser->module_ctx.super.deinit(user);
}

static int
mail_sieve_user_init(struct mail_user *user, struct sieve_storage **sstorage_r)
{
	/* delayed initialization of sieve storage until it's actually needed */
	struct mail_user_vfuncs *v = user->vlast;
	struct sieve_environment svenv;
	struct sieve_mail_user *suser;

	memset(&svenv, 0, sizeof(svenv));
	svenv.username = user->username;
	(void)mail_user_get_home(user, &svenv.home_dir);
	svenv.base_dir = user->set->base_dir;
	svenv.flags = SIEVE_FLAG_HOME_RELATIVE;

	suser = p_new(user->pool, struct sieve_mail_user, 1);
	suser->module_ctx.super = *v;
	user->vlast = &suser->module_ctx.super;
	v->deinit = mail_sieve_user_deinit;

	suser->svinst = sieve_init(&svenv, &mail_sieve_callbacks,
				   user, user->mail_debug);
	suser->sstorage = sieve_storage_create(suser->svinst, user->username,
					       svenv.home_dir, user->mail_debug);

	MODULE_CONTEXT_SET(user, sieve_user_module, suser);
	*sstorage_r = suser->sstorage;
	return 0;
}

static int sieve_attribute_unset_script(struct mail_storage *storage,
					struct sieve_storage *sstorage,
					const char *scriptname)
{
	struct sieve_script *script;
	const char *errstr;
	enum sieve_error error;
	int ret = 0;

	script = sieve_storage_script_init(sstorage, scriptname);
	ret = script == NULL ? -1 :
		sieve_storage_script_delete(&script);
	if (ret < 0) {
		errstr = sieve_storage_get_last_error(sstorage, &error);
		if (error == SIEVE_ERROR_NOT_FOUND) {
			/* already deleted, ignore */
			return 0;
		}
		mail_storage_set_critical(storage,
			"Failed to delete sieve script '%s': %s", scriptname,
			errstr);
		return -1;
	}
	return 0;
}

static int
sieve_attribute_set_active(struct mail_storage *storage,
			   struct sieve_storage *sstorage,
			   const struct mail_attribute_value *value)
{
	const char *scriptname;
	struct sieve_script *script;
	int ret;

	if (mailbox_attribute_value_to_string(storage, value, &scriptname) < 0)
		return -1;
	if (scriptname == NULL) {
		/* deactivate current script */
		if (sieve_storage_deactivate(sstorage) < 0) {
			mail_storage_set_critical(storage,
				"Failed to deactivate sieve: %s",
				sieve_storage_get_last_error(sstorage, NULL));
			return -1;
		}
		return 0;
	}

	/* activate specified script */
	script = sieve_storage_script_init(sstorage, scriptname);
	ret = script == NULL ? -1 :
		sieve_storage_script_activate(script);
	if (ret < 0) {
		mail_storage_set_critical(storage,
			"Failed to activate sieve script '%s': %s", scriptname,
			sieve_storage_get_last_error(sstorage, NULL));
	}
	if (script != NULL)
		sieve_script_unref(&script);
	return ret;
}

static int
sieve_attribute_set_sieve(struct mail_storage *storage,
			  const char *key,
			  const struct mail_attribute_value *value)
{
	struct sieve_storage *sstorage;
	struct sieve_save_context *save_ctx;
	struct istream *input;
	const char *scriptname;
	int ret = 0;

	if (mail_sieve_user_init(storage->user, &sstorage) < 0)
		return -1;

	if (strcmp(key, MAILBOX_ATTRIBUTE_SIEVE_ACTIVE) == 0)
		return sieve_attribute_set_active(storage, sstorage, value);
	if (strncmp(key, MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES,
		    strlen(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES)) != 0) {
		mail_storage_set_error(storage, MAIL_ERROR_NOTFOUND,
				       "Nonexistent sieve attribute");
		return -1;
	}
	scriptname = key + strlen(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES);

	if (value->value != NULL) {
		input = i_stream_create_from_data(value->value,
						  strlen(value->value));
		save_ctx = sieve_storage_save_init(sstorage, scriptname, input);
		i_stream_unref(&input);
	} else if (value->value_stream != NULL) {
		input = value->value_stream;
		save_ctx = sieve_storage_save_init(sstorage, scriptname, input);
	} else {
		return sieve_attribute_unset_script(storage, sstorage, scriptname);
	}

	if (save_ctx == NULL) {
		/* save initialization failed */
		mail_storage_set_critical(storage,
			"Failed to save sieve script '%s': %s", scriptname,
			sieve_storage_get_last_error(sstorage, NULL));
		return -1;
	}
	while (i_stream_read(input) > 0) {
		if (sieve_storage_save_continue(save_ctx) < 0) {
			mail_storage_set_critical(storage,
				"Failed to save sieve script '%s': %s", scriptname,
				sieve_storage_get_last_error(sstorage, NULL));
			ret = -1;
			break;
		}
	}
	if (input->stream_errno != 0) {
		errno = input->stream_errno;
		mail_storage_set_critical(storage,
			"Saving sieve script: read(%s) failed: %m",
			i_stream_get_name(input));
		ret = -1;
	}
	if (ret == 0 && sieve_storage_save_finish(save_ctx) < 0) {
		mail_storage_set_critical(storage,
			"Failed to save sieve script '%s': %s", scriptname,
			sieve_storage_get_last_error(sstorage, NULL));
		ret = -1;
	}
	if (ret < 0)
		sieve_storage_save_cancel(&save_ctx);
	else if (sieve_storage_save_commit(&save_ctx) < 0) {
		mail_storage_set_critical(storage,
			"Failed to save sieve script '%s': %s", scriptname,
			sieve_storage_get_last_error(sstorage, NULL));
		ret = -1;
	}
	return ret;
}

static int
sieve_attribute_set(struct mailbox_transaction_context *t,
		    enum mail_attribute_type type, const char *key,
		    const struct mail_attribute_value *value)
{
	union mailbox_module_context *sbox = SIEVE_MAIL_CONTEXT(t->box);

	if (t->box->storage->user->admin &&
	    type == MAIL_ATTRIBUTE_TYPE_PRIVATE &&
	    strncmp(key, MAILBOX_ATTRIBUTE_PREFIX_SIEVE,
		    strlen(MAILBOX_ATTRIBUTE_PREFIX_SIEVE)) == 0) {
		if (sieve_attribute_set_sieve(t->box->storage, key, value) < 0)
			return -1;
		if (value->value != NULL || value->value_stream != NULL)
			mail_index_attribute_set(t->itrans, TRUE, key);
		else
			mail_index_attribute_unset(t->itrans, TRUE, key);
		return 0;
	}
	return sbox->super.attribute_set(t, type, key, value);
}

static int
sieve_attribute_get_active(struct mail_storage *storage,
			   struct sieve_storage *sstorage,
			   struct mail_attribute_value *value_r)
{
	int ret;

	/* FIXME: set value_r->last_change */
	ret = sieve_storage_get_active_scriptname(sstorage, &value_r->value);
	if (ret < 0)
		mail_storage_set_internal_error(storage);
	return ret;
}

static int
sieve_attribute_get_sieve(struct mail_storage *storage, const char *key,
			  struct mail_attribute_value *value_r)
{
	struct sieve_storage *sstorage;
	struct sieve_script *script;
	struct istream *input;
	const struct stat *st;
	const char *scriptname, *errstr;
	enum sieve_error error;

	if (mail_sieve_user_init(storage->user, &sstorage) < 0)
		return -1;

	if (strcmp(key, MAILBOX_ATTRIBUTE_SIEVE_ACTIVE) == 0)
		return sieve_attribute_get_active(storage, sstorage, value_r);
	if (strncmp(key, MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES,
		    strlen(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES)) != 0)
		return 0;
	if ((value_r->flags & MAIL_ATTRIBUTE_VALUE_FLAG_INT_STREAMS) == 0) {
		mail_storage_set_error(storage, MAIL_ERROR_PARAMS,
			"Sieve attributes are available only as streams");
		return -1;
	}
	scriptname = key + strlen(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES);
	script = sieve_storage_script_init(sstorage, scriptname);
	if (script == NULL)
		errstr = sieve_storage_get_last_error(sstorage, &error);
	else if (sieve_script_get_stream(script, &input, &error) < 0) {
		errstr = t_strdup_printf("Error %d", error); //FIXME: ?
		sieve_script_unref(&script);
	}
	if (script == NULL) {
		if (error == SIEVE_ERROR_NOT_FOUND) {
			/* already deleted, but return the last_change */
			(void)sieve_storage_get_last_change(sstorage,
							    &value_r->last_change);
			return 0;
		}
		mail_storage_set_critical(storage,
			"Failed to access sieve script '%s': %s",
			scriptname, errstr);
		return -1;
	}
	i_stream_ref(input);
	value_r->value_stream = input;
	if (i_stream_stat(input, FALSE, &st) < 0) {
		mail_storage_set_critical(storage,
			"stat(%s) failed: %m", i_stream_get_name(input));
	} else {
		value_r->last_change = st->st_mtime;
	}
	sieve_script_unref(&script);
	return 1;
}

static int
sieve_attribute_get(struct mailbox_transaction_context *t,
		    enum mail_attribute_type type, const char *key,
		    struct mail_attribute_value *value_r)
{
	union mailbox_module_context *sbox = SIEVE_MAIL_CONTEXT(t->box);

	if (t->box->storage->user->admin &&
	    type == MAIL_ATTRIBUTE_TYPE_PRIVATE &&
	    strncmp(key, MAILBOX_ATTRIBUTE_PREFIX_SIEVE,
		    strlen(MAILBOX_ATTRIBUTE_PREFIX_SIEVE)) == 0)
		return sieve_attribute_get_sieve(t->box->storage, key, value_r);
	return sbox->super.attribute_get(t, type, key, value_r);
}

static int
sieve_attribute_iter_script_init(struct sieve_mailbox_attribute_iter *siter)
{
	struct sieve_storage *sstorage;

	if (mail_sieve_user_init(siter->iter.box->storage->user, &sstorage) < 0)
		return -1;

	siter->sieve_list = sieve_storage_list_init(sstorage);
	if (siter->sieve_list == NULL) {
		mail_storage_set_critical(siter->iter.box->storage,
			"Failed to iterate sieve scripts: %s",
			sieve_storage_get_last_error(sstorage, NULL));
		return -1;
	}
	siter->name = str_new(default_pool, 128);
	str_append(siter->name, MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES);
	return 0;
}

static struct mailbox_attribute_iter *
sieve_attribute_iter_init(struct mailbox *box, enum mail_attribute_type type,
			  const char *prefix)
{
	union mailbox_module_context *sbox = SIEVE_MAIL_CONTEXT(box);
	struct sieve_mailbox_attribute_iter *siter;

	siter = i_new(struct sieve_mailbox_attribute_iter, 1);
	siter->iter.box = box;
	siter->super = sbox->super.attribute_iter_init(box, type, prefix);

	if (box->storage->user->admin && type == MAIL_ATTRIBUTE_TYPE_PRIVATE &&
	    strncmp(prefix, MAILBOX_ATTRIBUTE_PREFIX_SIEVE,
		    strlen(prefix)) == 0) {
		if (sieve_attribute_iter_script_init(siter) < 0)
			siter->failed = TRUE;
	}
	return &siter->iter;
}

static const char *
sieve_attribute_iter_next_script(struct sieve_mailbox_attribute_iter *siter)
{
	struct sieve_storage *sstorage =
		SIEVE_MAIL_CONTEXT(siter->iter.box->storage);
	const char *scriptname;
	bool active;

	while ((scriptname = sieve_storage_list_next(siter->sieve_list, &active)) != NULL) {
		if (active)
			siter->have_active = TRUE;
		str_truncate(siter->name, strlen(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES));
		str_append(siter->name, scriptname);
		return str_c(siter->name);
	}
	if (sieve_storage_list_deinit(&siter->sieve_list) < 0) {
		mail_storage_set_critical(siter->iter.box->storage,
			"Failed to iterate sieve scripts: %s",
			sieve_storage_get_last_error(sstorage, NULL));
		siter->failed = TRUE;
		return NULL;
	}
	return siter->have_active ? MAILBOX_ATTRIBUTE_SIEVE_ACTIVE : NULL;
}

static const char *
sieve_attribute_iter_next(struct mailbox_attribute_iter *iter)
{
	struct sieve_mailbox_attribute_iter *siter =
		(struct sieve_mailbox_attribute_iter *)iter;
	union mailbox_module_context *sbox = SIEVE_MAIL_CONTEXT(iter->box);
	const char *key;

	if (siter->sieve_list != NULL) {
		if ((key = sieve_attribute_iter_next_script(siter)) != NULL)
			return key;
	}
	return sbox->super.attribute_iter_next(siter->super);
}

static int
sieve_attribute_iter_deinit(struct mailbox_attribute_iter *iter)
{
	struct sieve_mailbox_attribute_iter *siter =
		(struct sieve_mailbox_attribute_iter *)iter;
	union mailbox_module_context *sbox = SIEVE_MAIL_CONTEXT(iter->box);
	int ret = siter->failed ? -1 : 0;

	if (siter->super != NULL) {
		if (sbox->super.attribute_iter_deinit(siter->super) < 0)
			ret = -1;
	}
	if (siter->sieve_list != NULL)
		(void)sieve_storage_list_deinit(&siter->sieve_list);
	if (siter->name != NULL)
		str_free(&siter->name);
	i_free(siter);
	return ret;
}

static void sieve_mailbox_allocated(struct mailbox *box)
{
	struct mailbox_vfuncs *v = box->vlast;
	union mailbox_module_context *sbox;

	/* attribute syncing is done via INBOX */
	if (!box->inbox_user)
		return;

	sbox = p_new(box->pool, union mailbox_module_context, 1);
	sbox->super = *v;
	box->vlast = &sbox->super;
	v->attribute_set = sieve_attribute_set;
	v->attribute_get = sieve_attribute_get;
	v->attribute_iter_init = sieve_attribute_iter_init;
	v->attribute_iter_next = sieve_attribute_iter_next;
	v->attribute_iter_deinit = sieve_attribute_iter_deinit;
	MODULE_CONTEXT_SET_SELF(box, sieve_storage_module, sbox);
}

static struct mail_storage_hooks doveadm_sieve_mail_storage_hooks = {
	.mailbox_allocated = sieve_mailbox_allocated
};

void doveadm_sieve_plugin_init(struct module *module)
{
	mail_storage_hooks_add_forced(module, &doveadm_sieve_mail_storage_hooks);
}

void doveadm_sieve_plugin_deinit(void)
{
	/* the hooks array is freed already */
	/*mail_storage_hooks_remove(&doveadm_sieve_mail_storage_hooks);*/
}
