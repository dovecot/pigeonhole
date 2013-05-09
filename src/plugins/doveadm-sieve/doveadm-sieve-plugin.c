/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "ioloop.h"
#include "istream.h"
#include "istream-concat.h"
#include "sieve-script.h"
#include "sieve-script-file.h"
#include "sieve-storage.h"
#include "sieve-storage-list.h"
#include "sieve-storage-save.h"
#include "sieve-storage-script.h"
#include "mail-storage-private.h"

#define SIEVE_MAIL_CONTEXT(obj) \
	MODULE_CONTEXT(obj, sieve_storage_module)
#define SIEVE_USER_CONTEXT(obj) \
	MODULE_CONTEXT(obj, sieve_user_module)

struct sieve_mail_user {
	union mail_user_module_context module_ctx;

	struct sieve_instance *svinst;
	struct sieve_storage *sieve_storage;
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

	sieve_storage_free(suser->sieve_storage);
	sieve_deinit(&suser->svinst);

	suser->module_ctx.super.deinit(user);
}

static int
mail_sieve_user_init
(struct mail_user *user, struct sieve_storage **svstorage_r)
{
	struct sieve_mail_user *suser = SIEVE_USER_CONTEXT(user);
	enum sieve_storage_flags storage_flags = SIEVE_STORAGE_FLAG_SYNCHRONIZING;
	struct mail_user_vfuncs *v = user->vlast;
	struct sieve_environment svenv;

	if (suser != NULL) {
		*svstorage_r = suser->sieve_storage;
		return 0;
	}

	/* Delayed initialization of sieve storage until it's actually needed */
	memset(&svenv, 0, sizeof(svenv));
	svenv.username = user->username;
	(void)mail_user_get_home(user, &svenv.home_dir);
	svenv.base_dir = user->set->base_dir;
	svenv.flags = SIEVE_FLAG_HOME_RELATIVE;

	suser = p_new(user->pool, struct sieve_mail_user, 1);
	suser->module_ctx.super = *v;
	user->vlast = &suser->module_ctx.super;
	v->deinit = mail_sieve_user_deinit;

	if (user->mail_debug)
		storage_flags |= SIEVE_STORAGE_FLAG_DEBUG;

	suser->svinst = sieve_init(&svenv, &mail_sieve_callbacks,
				   user, user->mail_debug);
	suser->sieve_storage = sieve_storage_create(suser->svinst, user,
					       svenv.home_dir, storage_flags);

	MODULE_CONTEXT_SET(user, sieve_user_module, suser);
	*svstorage_r = suser->sieve_storage;
	return 0;
}

static int sieve_attribute_unset_script(struct mail_storage *storage,
					struct sieve_storage *svstorage,
					const char *scriptname)
{
	struct sieve_script *script;
	const char *errstr;
	enum sieve_error error;
	int ret = 0;

	script = sieve_storage_script_init(svstorage, scriptname);
	ret = script == NULL ? -1 :
		sieve_storage_script_delete(&script);
	if (ret < 0) {
		errstr = sieve_storage_get_last_error(svstorage, &error);
		if (error == SIEVE_ERROR_NOT_FOUND) {
			/* already deleted, ignore */
			return 0;
		}
		mail_storage_set_critical(storage,
			"Failed to delete Sieve script '%s': %s", scriptname,
			errstr);
		return -1;
	}
	return 0;
}

static int
sieve_attribute_set_active(struct mail_storage *storage,
			   struct sieve_storage *svstorage,
			   const struct mail_attribute_value *value)
{
	const char *scriptname;
	struct sieve_script *script;
	int ret;

	if (mailbox_attribute_value_to_string(storage, value, &scriptname) < 0)
		return -1;
	i_assert(scriptname[0] == MAILBOX_ATTRIBUTE_SIEVE_DEFAULT_LINK);
	scriptname++;

	if (scriptname == NULL) {
		/* don't affect non-link active script */
		if ((ret=sieve_storage_active_script_is_no_link(svstorage)) != 0) {
			if (ret < 0) {
				mail_storage_set_internal_error(storage);
				return -1;
			}
			return 0;
		}

		/* deactivate current script */
		if (sieve_storage_deactivate(svstorage, value->last_change) < 0) {
			mail_storage_set_critical(storage,
				"Failed to deactivate Sieve: %s",
				sieve_storage_get_last_error(svstorage, NULL));
			return -1;
		}
		return 0;
	}

	/* activate specified script */
	script = sieve_storage_script_init(svstorage, scriptname);
	ret = script == NULL ? -1 :
		sieve_storage_script_activate(script, value->last_change);
	if (ret < 0) {
		mail_storage_set_critical(storage,
			"Failed to activate Sieve script '%s': %s", scriptname,
			sieve_storage_get_last_error(svstorage, NULL));
	}
	if (script != NULL)
		sieve_script_unref(&script);
	sieve_storage_set_modified(svstorage, value->last_change);
	return ret;
}

static int
sieve_attribute_unset_active_script(struct mail_storage *storage,
			   struct sieve_storage *svstorage, time_t last_change)
{
	int ret;

	if ((ret=sieve_storage_active_script_is_no_link(svstorage)) <= 0) {
		if (ret < 0)
			mail_storage_set_internal_error(storage);
		return ret;
	}

	if (sieve_storage_deactivate(svstorage, last_change) < 0) {
		mail_storage_set_critical(storage,
			"Failed to deactivate sieve: %s",
			sieve_storage_get_last_error(svstorage, NULL));
		return -1;
	}
	return 0;
}

static int
sieve_attribute_set_active_script(struct mail_storage *storage,
			   struct sieve_storage *svstorage,
			   const struct mail_attribute_value *value)
{
	struct istream *input;

	if (value->value != NULL) {
		input = i_stream_create_from_data(value->value, strlen(value->value));
	} else if (value->value_stream != NULL) {
		input = value->value_stream;
		i_stream_ref(input);
	} else {
		return sieve_attribute_unset_active_script(storage, svstorage, value->last_change);
	}
	/* skip over the 'S' type */
	i_stream_skip(input, 1);

	if (sieve_storage_save_as_active_script
		(svstorage, input, value->last_change) < 0) {
		mail_storage_set_critical(storage,
			"Failed to save active sieve script: %s",
			sieve_storage_get_last_error(svstorage, NULL));
		i_stream_unref(&input);
		return -1;
	}

	sieve_storage_set_modified(svstorage, value->last_change);
	i_stream_unref(&input);
	return 0;
}

static int
sieve_attribute_set_default(struct mail_storage *storage,
			    struct sieve_storage *svstorage,
			    const struct mail_attribute_value *value)
{
	const unsigned char *data;
	size_t size;
	ssize_t ret;
	char type;

	if (value->value != NULL) {
		type = value->value[0];
	} else {
		ret = i_stream_read_data(value->value_stream, &data, &size, 0);
		if (ret == -1) {
			mail_storage_set_critical(storage, "read(%s) failed: %m",
				i_stream_get_name(value->value_stream));
			return -1;
		}
		i_assert(ret > 0);
		type = data[0];
	}
	if (type == MAILBOX_ATTRIBUTE_SIEVE_DEFAULT_LINK)
		return sieve_attribute_set_active(storage, svstorage, value);
	if (type == MAILBOX_ATTRIBUTE_SIEVE_DEFAULT_SCRIPT)
		return sieve_attribute_set_active_script(storage, svstorage, value);
	mail_storage_set_error(storage, MAIL_ERROR_PARAMS,
			       "Invalid value for default sieve attribute");
	return -1;
}

static int
sieve_attribute_set_sieve(struct mail_storage *storage,
			  const char *key,
			  const struct mail_attribute_value *value)
{
	struct sieve_storage *svstorage;
	struct sieve_save_context *save_ctx;
	struct istream *input;
	const char *scriptname;
	int ret = 0;

	if (mail_sieve_user_init(storage->user, &svstorage) < 0)
		return -1;

	if (strcmp(key, MAILBOX_ATTRIBUTE_SIEVE_DEFAULT) == 0)
		return sieve_attribute_set_default(storage, svstorage, value);
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
		save_ctx = sieve_storage_save_init(svstorage, scriptname, input);
		i_stream_unref(&input);
	} else if (value->value_stream != NULL) {
		input = value->value_stream;
		save_ctx = sieve_storage_save_init(svstorage, scriptname, input);
	} else {
		return sieve_attribute_unset_script(storage, svstorage, scriptname);
	}

	sieve_storage_save_set_mtime(save_ctx, value->last_change);

	if (save_ctx == NULL) {
		/* save initialization failed */
		mail_storage_set_critical(storage,
			"Failed to save sieve script '%s': %s", scriptname,
			sieve_storage_get_last_error(svstorage, NULL));
		return -1;
	}
	while (i_stream_read(input) > 0) {
		if (sieve_storage_save_continue(save_ctx) < 0) {
			mail_storage_set_critical(storage,
				"Failed to save sieve script '%s': %s", scriptname,
				sieve_storage_get_last_error(svstorage, NULL));
			ret = -1;
			break;
		}
	}
	i_assert(input->eof);
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
			sieve_storage_get_last_error(svstorage, NULL));
		ret = -1;
	}
	if (ret < 0)
		sieve_storage_save_cancel(&save_ctx);
	else if (sieve_storage_save_commit(&save_ctx) < 0) {
		mail_storage_set_critical(storage,
			"Failed to save sieve script '%s': %s", scriptname,
			sieve_storage_get_last_error(svstorage, NULL));
		ret = -1;
	}
	return ret;
}

static int
sieve_attribute_set(struct mailbox_transaction_context *t,
		    enum mail_attribute_type type, const char *key,
		    const struct mail_attribute_value *value)
{
	struct mail_user *user = t->box->storage->user;
	union mailbox_module_context *sbox = SIEVE_MAIL_CONTEXT(t->box);
	time_t ts = value->last_change != 0 ? value->last_change : ioloop_time;

	if (t->box->storage->user->dsyncing &&
	    type == MAIL_ATTRIBUTE_TYPE_PRIVATE &&
	    strncmp(key, MAILBOX_ATTRIBUTE_PREFIX_SIEVE,
		    strlen(MAILBOX_ATTRIBUTE_PREFIX_SIEVE)) == 0) {
		if (sieve_attribute_set_sieve(t->box->storage, key, value) < 0)
			return -1;
		if (user->mail_debug)
			i_debug("doveadm-sieve: Assigned value for key `%s'", key);
		/* FIXME: set value len to sieve script size / active name
		   length */
		if (value->value != NULL || value->value_stream != NULL)
			mail_index_attribute_set(t->itrans, TRUE, key, ts, 0);
		else
			mail_index_attribute_unset(t->itrans, TRUE, key, ts);
		return 0;
	}
	return sbox->super.attribute_set(t, type, key, value);
}

static int
sieve_attribute_retrieve_script(struct mail_storage *storage,
			   struct sieve_storage *svstorage, struct sieve_script *script,
			   bool add_type_prefix,
			   struct mail_attribute_value *value_r, const char **errorstr_r)
{
	char type = MAILBOX_ATTRIBUTE_SIEVE_DEFAULT_SCRIPT;
	struct istream *input, *inputs[3];
	const struct stat *st;
	enum sieve_error error;

	if (script == NULL)
		*errorstr_r = sieve_storage_get_last_error(svstorage, &error);
	else if (sieve_script_get_stream(script, &input, &error) < 0)
		sieve_script_unref(&script);
	
	if (script == NULL) {
		if (error == SIEVE_ERROR_NOT_FOUND) {
			/* already deleted, but return the last_change */
			(void)sieve_storage_get_last_change(svstorage,
							    &value_r->last_change);
			return 0;
		}
		*errorstr_r = sieve_storage_get_last_error(svstorage, &error);
		return -1;
	}
	if (i_stream_stat(input, FALSE, &st) < 0) {
		mail_storage_set_critical(storage,
			"stat(%s) failed: %m", i_stream_get_name(input));
	} else {
		value_r->last_change = st->st_mtime;
	}
	if (!add_type_prefix) {
		i_stream_ref(input);
		value_r->value_stream = input;
	} else {
		inputs[0] = i_stream_create_from_data(&type, 1);
		inputs[1] = input;
		inputs[2] = NULL;
		value_r->value_stream = i_stream_create_concat(inputs);
	}
	sieve_script_unref(&script);
	return 1;
}

static int
sieve_attribute_get_active_script(struct mail_storage *storage,
			   struct sieve_storage *svstorage,
			   struct mail_attribute_value *value_r)
{
	struct sieve_script *script;
	const char *errstr;
	int ret;

	if ((ret=sieve_storage_active_script_is_no_link(svstorage)) <= 0) {
		if (ret == 0 && sieve_storage_active_script_get_last_change
			(svstorage, &value_r->last_change) < 0) {
			ret = -1;
		}
		if (ret < 0)
			mail_storage_set_internal_error(storage);
		return ret;
	}

	if ((script=sieve_storage_active_script_get(svstorage)) == NULL)
		return 0;
	if ((ret=sieve_attribute_retrieve_script
		(storage, svstorage, script, TRUE, value_r, &errstr)) < 0) {
		mail_storage_set_critical(storage,
			"Failed to access active sieve script: %s", errstr);
	}
	return ret;
}

static int
sieve_attribute_get_default(struct mail_storage *storage,
			    struct sieve_storage *svstorage,
			    struct mail_attribute_value *value_r)
{
	const char *scriptname;
	int ret;

	ret = sieve_storage_active_script_get_name(svstorage, &scriptname);
	if (ret == 0)
		return sieve_attribute_get_active_script(storage, svstorage, value_r);

	if (ret > 0) {
		value_r->value = t_strdup_printf("%c%s",
			MAILBOX_ATTRIBUTE_SIEVE_DEFAULT_LINK, scriptname);
		if (sieve_storage_active_script_get_last_change
				(svstorage, &value_r->last_change) < 0)
			ret = -1;
	}
	if (ret < 0)
		mail_storage_set_internal_error(storage);
	return ret;
}

static int
sieve_attribute_get_sieve(struct mail_storage *storage, const char *key,
			  struct mail_attribute_value *value_r)
{
	struct sieve_storage *svstorage;
	struct sieve_script *script;
	const char *scriptname, *errstr;
	int ret;

	if (mail_sieve_user_init(storage->user, &svstorage) < 0)
		return -1;

	if (strcmp(key, MAILBOX_ATTRIBUTE_SIEVE_DEFAULT) == 0)
		return sieve_attribute_get_default(storage, svstorage, value_r);
	if (strncmp(key, MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES,
		    strlen(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES)) != 0)
		return 0;
	if ((value_r->flags & MAIL_ATTRIBUTE_VALUE_FLAG_INT_STREAMS) == 0) {
		mail_storage_set_error(storage, MAIL_ERROR_PARAMS,
			"Sieve attributes are available only as streams");
		return -1;
	}
	scriptname = key + strlen(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES);
	script = sieve_storage_script_init(svstorage, scriptname);
	if ((ret=sieve_attribute_retrieve_script
		(storage, svstorage, script, FALSE, value_r, &errstr)) < 0) {
		mail_storage_set_critical(storage,
			"Failed to access sieve script '%s': %s",
			scriptname, errstr);
	}
	return ret;
}

static int
sieve_attribute_get(struct mailbox_transaction_context *t,
		    enum mail_attribute_type type, const char *key,
		    struct mail_attribute_value *value_r)
{
	union mailbox_module_context *sbox = SIEVE_MAIL_CONTEXT(t->box);
	struct mail_user *user = t->box->storage->user;
	int ret;

	if (t->box->storage->user->dsyncing &&
	    type == MAIL_ATTRIBUTE_TYPE_PRIVATE &&
	    strncmp(key, MAILBOX_ATTRIBUTE_PREFIX_SIEVE,
		    strlen(MAILBOX_ATTRIBUTE_PREFIX_SIEVE)) == 0) {

		ret = sieve_attribute_get_sieve(t->box->storage, key, value_r);
		if (ret >= 0 && user->mail_debug) {
			struct tm *tm = localtime(&value_r->last_change);
			char str[256];
			const char *timestamp = "";
	
			if (strftime(str, sizeof(str),
				" (last change: %Y-%m-%d %H:%M:%S)", tm) > 0)
				timestamp = str;

			if (ret > 0) {
				i_debug("doveadm-sieve: Retrieved value for key `%s'%s",
					key, timestamp);
			} else {
				i_debug("doveadm-sieve: Value missing for key `%s'%s",
					key, timestamp);
			}
		}
		return ret;
	}
	return sbox->super.attribute_get(t, type, key, value_r);
}

static int
sieve_attribute_iter_script_init(struct sieve_mailbox_attribute_iter *siter)
{
	struct mail_user *user = siter->iter.box->storage->user;
	struct sieve_storage *svstorage;

	if (user->mail_debug)
		i_debug("doveadm-sieve: Iterating Sieve mailbox attributes");

	if (mail_sieve_user_init(user, &svstorage) < 0)
		return -1;

	siter->sieve_list = sieve_storage_list_init(svstorage);
	if (siter->sieve_list == NULL) {
		mail_storage_set_critical(siter->iter.box->storage,
			"Failed to iterate sieve scripts: %s",
			sieve_storage_get_last_error(svstorage, NULL));
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

	if (box->storage->user->dsyncing &&
	    type == MAIL_ATTRIBUTE_TYPE_PRIVATE &&
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
	struct mail_user *user = siter->iter.box->storage->user;
	struct sieve_mail_user *suser = SIEVE_USER_CONTEXT(user);
	struct sieve_storage *svstorage = suser->sieve_storage;
	const char *scriptname;
	bool active;
	int ret;

	/* Iterate through all scripts in sieve_dir */
	while ((scriptname = sieve_storage_list_next(siter->sieve_list, &active))
		!= NULL) {
		if (active)
			siter->have_active = TRUE;
		str_truncate(siter->name, strlen(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES));
		str_append(siter->name, scriptname);
		return str_c(siter->name);
	}
	if (sieve_storage_list_deinit(&siter->sieve_list) < 0) {
		mail_storage_set_critical(siter->iter.box->storage,
			"Failed to iterate sieve scripts: %s",
			sieve_storage_get_last_error(svstorage, NULL));
		siter->failed = TRUE;
		return NULL;
	}

	/* Check whether active script is a proper symlink or a regular file */
	if ((ret=sieve_storage_active_script_is_no_link(svstorage)) < 0) {
		mail_storage_set_critical(siter->iter.box->storage,
			"Failed to iterate sieve scripts: %s",
			sieve_storage_get_last_error(svstorage, NULL));
		return NULL;
	}

	/* Regular file */
	if (ret > 0)
		return MAILBOX_ATTRIBUTE_SIEVE_DEFAULT;

	/* Symlink or none active */
	return siter->have_active ? MAILBOX_ATTRIBUTE_SIEVE_DEFAULT : NULL;
}

static const char *
sieve_attribute_iter_next(struct mailbox_attribute_iter *iter)
{
	struct sieve_mailbox_attribute_iter *siter =
		(struct sieve_mailbox_attribute_iter *)iter;
	union mailbox_module_context *sbox = SIEVE_MAIL_CONTEXT(iter->box);
	struct mail_user *user = iter->box->storage->user;
	const char *key;

	if (siter->sieve_list != NULL) {
		if ((key = sieve_attribute_iter_next_script(siter)) != NULL) {
			if (user->mail_debug) {
				i_debug("doveadm-sieve: Iterating Sieve mailbox attribute: %s", key);
			}
			return key;
		}
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
