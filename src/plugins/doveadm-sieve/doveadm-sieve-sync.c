/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "ioloop.h"
#include "time-util.h"
#include "istream.h"
#include "istream-concat.h"
#include "mail-storage-private.h"

#include "sieve.h"
#include "sieve-script.h"
#include "sieve-storage.h"

#include "doveadm-sieve-plugin.h"

#define SIEVE_MAIL_CONTEXT(obj) \
	MODULE_CONTEXT_REQUIRE(obj, sieve_storage_module)
#define SIEVE_USER_CONTEXT(obj) \
	MODULE_CONTEXT_REQUIRE(obj, sieve_user_module)

struct sieve_mail_user {
	union mail_user_module_context module_ctx;
	struct event *event;

	struct sieve_instance *svinst;
	struct sieve_storage *sieve_storage;
};

struct sieve_mailbox_attribute_iter {
	struct mailbox_attribute_iter iter;
	struct mailbox_attribute_iter *super;

	struct sieve_storage_list_context *sieve_list;
	string_t *name;

	bool failed;
	bool have_active;
};

static MODULE_CONTEXT_DEFINE_INIT(sieve_storage_module,
				  &mail_storage_module_register);
static MODULE_CONTEXT_DEFINE_INIT(sieve_user_module,
				  &mail_user_module_register);

static void mail_sieve_user_deinit(struct mail_user *user)
{
	struct sieve_mail_user *suser = SIEVE_USER_CONTEXT(user);

	if (suser->svinst != NULL) {
		sieve_storage_unref(&suser->sieve_storage);
		sieve_deinit(&suser->svinst);
	}

	event_unref(&suser->event);
	suser->module_ctx.super.deinit(user);
}

static int
mail_sieve_user_init(struct mail_user *user, struct sieve_storage **svstorage_r)
{
	struct sieve_mail_user *suser = SIEVE_USER_CONTEXT(user);
	enum sieve_storage_flags storage_flags =
		SIEVE_STORAGE_FLAG_READWRITE |
		SIEVE_STORAGE_FLAG_SYNCHRONIZING;
	struct sieve_environment svenv;
	enum sieve_error error_code;

	*svstorage_r = NULL;

	if (suser->svinst != NULL) {
		*svstorage_r = suser->sieve_storage;
		return (suser->sieve_storage != NULL ? 1 : 0);
	}

	/* Delayed initialization of sieve storage until it's actually needed */
	i_zero(&svenv);
	svenv.event_parent = user->event;
	svenv.username = user->username;
	(void)mail_user_get_home(user, &svenv.home_dir);
	svenv.base_dir = user->set->base_dir;
	svenv.flags = SIEVE_FLAG_HOME_RELATIVE;

	if (sieve_init(&svenv, NULL, user, user->set->mail_debug,
		       &suser->svinst) < 0)
		return -1;

	if (sieve_storage_create_personal(suser->svinst, user,
					  SIEVE_SCRIPT_CAUSE_ANY, storage_flags,
					  &suser->sieve_storage,
					  &error_code) < 0) {
		switch (error_code) {
		/* Sieve disabled for user */
		case SIEVE_ERROR_NOT_POSSIBLE:
		/* Sieve script not found */
		case SIEVE_ERROR_NOT_FOUND:
			return 0;
		default:
			break;
		}
		return -1;
	}

	*svstorage_r = suser->sieve_storage;
	return 1;
}

static int sieve_attribute_unset_script(struct mail_storage *storage,
					struct sieve_storage *svstorage,
					const char *scriptname)
{
	struct sieve_script *script;
	const char *error;
	enum sieve_error error_code;
	int ret = 0;

	ret = sieve_storage_open_script(svstorage, scriptname,
					&script, NULL);
	if (ret == 0) {
		ret = sieve_script_delete(script, TRUE);
		sieve_script_unref(&script);
	}

	if (ret < 0) {
		error = sieve_storage_get_last_error(svstorage, &error_code);
		if (error_code == SIEVE_ERROR_NOT_FOUND) {
			/* already deleted, ignore */
			return 0;
		}
		mail_storage_set_critical(
			storage, "Failed to delete Sieve script '%s': %s",
			scriptname, error);
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
	time_t last_change = (value->last_change == 0 ?
			      ioloop_time : value->last_change);
	int ret;

	if (mailbox_attribute_value_to_string(storage, value, &scriptname) < 0)
		return -1;

	if (scriptname == NULL) {
		/* Don't affect non-link active script */
		if ((ret=sieve_storage_is_singular(svstorage)) != 0) {
			if (ret < 0) {
				mail_storage_set_internal_error(storage);
				return -1;
			}
			return 0;
		}

		/* Deactivate current script */
		if (sieve_storage_deactivate(svstorage, last_change) < 0) {
			mail_storage_set_critical(
				storage, "Failed to deactivate Sieve: %s",
				sieve_storage_get_last_error(svstorage, NULL));
			return -1;
		}
		return 0;
	}
	i_assert(scriptname[0] == MAILBOX_ATTRIBUTE_SIEVE_DEFAULT_LINK);
	scriptname++;

	/* Activate specified script */
	ret = 0;
	if (sieve_storage_open_script(svstorage, scriptname,
				      &script, NULL) < 0 ||
	    sieve_script_activate(script, last_change) < 0) {
		mail_storage_set_critical(
			storage, "Failed to activate Sieve script '%s': %s",
			scriptname,
			sieve_storage_get_last_error(svstorage, NULL));
		ret = -1;
	}
	sieve_script_unref(&script);
	sieve_storage_set_modified(svstorage, last_change);
	return ret;
}

static int
sieve_attribute_unset_active_script(struct mail_storage *storage,
				    struct sieve_storage *svstorage,
				    time_t last_change)
{
	int ret;

	ret = sieve_storage_is_singular(svstorage);
	if (ret != 0) {
		if (ret < 0)
			mail_storage_set_internal_error(storage);
		return ret;
	}

	if (sieve_storage_deactivate(svstorage, last_change) < 0) {
		mail_storage_set_critical(
			storage, "Failed to deactivate sieve: %s",
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
	time_t last_change = (value->last_change == 0 ?
			      ioloop_time : value->last_change);

	if (value->value != NULL) {
		input = i_stream_create_from_data(value->value,
						  strlen(value->value));
	} else if (value->value_stream != NULL) {
		input = value->value_stream;
		i_stream_ref(input);
	} else {
		return sieve_attribute_unset_active_script(storage, svstorage,
							   last_change);
	}
	/* Skip over the 'S' type */
	i_stream_skip(input, 1);

	if (sieve_storage_save_as_active(svstorage, input, last_change) < 0) {
		mail_storage_set_critical(
			storage, "Failed to save active sieve script: %s",
			sieve_storage_get_last_error(svstorage, NULL));
		i_stream_unref(&input);
		return -1;
	}

	sieve_storage_set_modified(svstorage, last_change);
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
	} else if (value->value_stream != NULL) {
		ret = i_stream_read_more(value->value_stream, &data, &size);
		if (ret == -1) {
			mail_storage_set_critical(storage, "read(%s) failed: %m",
				i_stream_get_name(value->value_stream));
			return -1;
		}
		i_assert(ret > 0);
		type = data[0];
	} else {
		type = MAILBOX_ATTRIBUTE_SIEVE_DEFAULT_SCRIPT;
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
	struct sieve_storage_save_context *save_ctx;
	struct istream *input;
	const char *scriptname;
	int ret;

	ret = mail_sieve_user_init(storage->user, &svstorage);
	if (ret <= 0) {
		if (ret == 0) {
			mail_storage_set_error(storage, MAIL_ERROR_NOTFOUND,
					       "Sieve not enabled for user");
		} else {
			mail_storage_set_internal_error(storage);
		}
		return -1;
	}

	if (strcmp(key, MAILBOX_ATTRIBUTE_SIEVE_DEFAULT) == 0)
		return sieve_attribute_set_default(storage, svstorage, value);
	if (!str_begins(key, MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES, &scriptname)) {
		mail_storage_set_error(storage, MAIL_ERROR_NOTFOUND,
				       "Nonexistent sieve attribute");
		return -1;
	}

	if (value->value != NULL) {
		input = i_stream_create_from_data(value->value,
						  strlen(value->value));
		save_ctx = sieve_storage_save_init(svstorage, scriptname,
						   input);
	} else if (value->value_stream != NULL) {
		input = value->value_stream;
		i_stream_ref(input);
		save_ctx = sieve_storage_save_init(svstorage, scriptname,
						   input);
	} else {
		return sieve_attribute_unset_script(storage, svstorage,
						    scriptname);
	}

	if (save_ctx == NULL) {
		/* Save initialization failed */
		mail_storage_set_critical(
			storage, "Failed to save sieve script '%s': %s",
			scriptname,
			sieve_storage_get_last_error(svstorage, NULL));
		i_stream_unref(&input);
		return -1;
	}

	if (value->last_change != 0)
		sieve_storage_save_set_mtime(save_ctx, value->last_change);

	ret = 0;
	while (input->stream_errno == 0 &&
		!i_stream_read_eof(input)) {
		if (sieve_storage_save_continue(save_ctx) < 0) {
			mail_storage_set_critical(
				storage, "Failed to save sieve script '%s': %s",
				scriptname,
				sieve_storage_get_last_error(svstorage, NULL));
			ret = -1;
			break;
		}
	}
	if (input->stream_errno != 0) {
		errno = input->stream_errno;
		mail_storage_set_critical(
			storage, "Saving sieve script: read(%s) failed: %m",
			i_stream_get_name(input));
		ret = -1;
	}
	i_assert(input->eof || ret < 0);
	if (ret == 0 && sieve_storage_save_finish(save_ctx) < 0) {
		mail_storage_set_critical(
			storage, "Failed to save sieve script '%s': %s",
			scriptname,
			sieve_storage_get_last_error(svstorage, NULL));
		ret = -1;
	}
	if (ret < 0)
		sieve_storage_save_cancel(&save_ctx);
	else if (sieve_storage_save_commit(&save_ctx) < 0) {
		mail_storage_set_critical(
			storage, "Failed to save sieve script '%s': %s",
			scriptname,
			sieve_storage_get_last_error(svstorage, NULL));
		ret = -1;
	}
	i_stream_unref(&input);
	return ret;
}

static int
sieve_attribute_set(struct mailbox_transaction_context *t,
		    enum mail_attribute_type type, const char *key,
		    const struct mail_attribute_value *value)
{
	struct mail_user *user = t->box->storage->user;
	struct sieve_mail_user *suser = SIEVE_USER_CONTEXT(user);
	union mailbox_module_context *sbox = SIEVE_MAIL_CONTEXT(t->box);

	if (t->box->storage->user->dsyncing &&
	    type == MAIL_ATTRIBUTE_TYPE_PRIVATE &&
	    str_begins_with(key, MAILBOX_ATTRIBUTE_PREFIX_SIEVE)) {
		time_t ts = (value->last_change != 0 ?
			     value->last_change : ioloop_time);
		const char *change;

		if (sieve_attribute_set_sieve(t->box->storage, key, value) < 0)
			return -1;

		if (value->last_change != 0) {
			change = t_strflocaltime(
				"(last change: %Y-%m-%d %H:%M:%S)",
				value->last_change);
		} else {
			change = t_strflocaltime(
				"(time: %Y-%m-%d %H:%M:%S)",
				ioloop_time);
		}
		e_debug(suser->event, "Assigned value for key '%s' %s",
			key, change);

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
				struct sieve_storage *svstorage,
				struct sieve_script *script,
				bool add_type_prefix,
				struct mail_attribute_value *value_r,
				const char **error_r)
{
	static char type = MAILBOX_ATTRIBUTE_SIEVE_DEFAULT_SCRIPT;
	struct istream *input, *inputs[3];
	const struct stat *st;
	enum sieve_error error_code;

	if (script == NULL)
		*error_r = sieve_storage_get_last_error(svstorage, &error_code);
	else if (sieve_script_get_stream(script, &input, &error_code) < 0)
		sieve_script_unref(&script);

	if (script == NULL) {
		if (error_code == SIEVE_ERROR_NOT_FOUND) {
			/* Already deleted, but return the last_change */
			(void)sieve_storage_get_last_change(
				svstorage, &value_r->last_change);
			return 0;
		}
		*error_r = sieve_storage_get_last_error(svstorage, &error_code);
		return -1;
	}
	if (i_stream_stat(input, FALSE, &st) < 0) {
		mail_storage_set_critical(storage, "stat(%s) failed: %m",
					  i_stream_get_name(input));
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
		i_stream_unref(&inputs[0]);
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
	const char *error;
	int ret;

	ret = sieve_storage_is_singular(svstorage);
	if (ret <= 0) {
		if (ret == 0 &&
		    sieve_storage_active_script_get_last_change(
			svstorage, &value_r->last_change) < 0) {
			ret = -1;
		}
		if (ret < 0)
			mail_storage_set_internal_error(storage);
		return ret;
	}

	if (sieve_storage_active_script_open(svstorage, &script, NULL) < 0)
		return 0;

	ret = sieve_attribute_retrieve_script(storage, svstorage, script, TRUE,
					      value_r, &error);
	if (ret < 0) {
		mail_storage_set_critical(
			storage, "Failed to access active sieve script: %s",
			error);
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
	if (ret == 0) {
		return sieve_attribute_get_active_script(storage, svstorage,
							 value_r);
	}

	if (ret > 0) {
		value_r->value = t_strdup_printf(
			"%c%s", MAILBOX_ATTRIBUTE_SIEVE_DEFAULT_LINK,
			scriptname);
		if (sieve_storage_active_script_get_last_change(
			svstorage, &value_r->last_change) < 0)
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
	const char *scriptname, *error;
	int ret;

	ret = mail_sieve_user_init(storage->user, &svstorage);
	if (ret <= 0) {
		if (ret < 0)
			mail_storage_set_internal_error(storage);
		return ret;
	}

	if (strcmp(key, MAILBOX_ATTRIBUTE_SIEVE_DEFAULT) == 0)
		return sieve_attribute_get_default(storage, svstorage, value_r);
	if (!str_begins(key, MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES, &scriptname))
		return 0;
	if ((value_r->flags & MAIL_ATTRIBUTE_VALUE_FLAG_INT_STREAMS) == 0) {
		mail_storage_set_error(
			storage, MAIL_ERROR_PARAMS,
			"Sieve attributes are available only as streams");
		return -1;
	}
	ret = sieve_storage_open_script(svstorage, scriptname, &script, NULL);
	if (ret < 0) {
		enum sieve_error error_code;

		error = sieve_storage_get_last_error(svstorage, &error_code);
		if (error_code == SIEVE_ERROR_NOT_FOUND)
			ret = 0;
	} else {
		ret = sieve_attribute_retrieve_script(storage, svstorage,
						      script, FALSE,
						      value_r, &error);
	}
	if (ret < 0) {
		mail_storage_set_critical(
			storage, "Failed to access sieve script '%s': %s",
			scriptname, error);
	}
	return ret;
}

static int
sieve_attribute_get(struct mailbox *box,
		    enum mail_attribute_type type, const char *key,
		    struct mail_attribute_value *value_r)
{
	union mailbox_module_context *sbox = SIEVE_MAIL_CONTEXT(box);
	struct mail_user *user = box->storage->user;
	struct sieve_mail_user *suser = SIEVE_USER_CONTEXT(user);
	int ret;

	if (box->storage->user->dsyncing &&
	    type == MAIL_ATTRIBUTE_TYPE_PRIVATE &&
	    str_begins_with(key, MAILBOX_ATTRIBUTE_PREFIX_SIEVE)) {
		ret = sieve_attribute_get_sieve(box->storage, key, value_r);
		if (ret >= 0) {
			struct tm *tm = localtime(&value_r->last_change);
			char str[256];
			const char *timestamp = "";

			if (strftime(str, sizeof(str),
				     " (last change: %Y-%m-%d %H:%M:%S)",
				     tm) > 0)
				timestamp = str;

			if (ret > 0) {
				e_debug(suser->event,
					"Retrieved value for key '%s'%s",
					key, timestamp);
			} else {
				e_debug(suser->event,
					"Value missing for key '%s'%s",
					key, timestamp);
			}
		}
		return ret;
	}
	return sbox->super.attribute_get(box, type, key, value_r);
}

static int
sieve_attribute_iter_script_init(struct sieve_mailbox_attribute_iter *siter)
{
	struct mail_user *user = siter->iter.box->storage->user;
	struct sieve_mail_user *suser = SIEVE_USER_CONTEXT(user);
	struct sieve_storage *svstorage;
	int ret;

	e_debug(suser->event, "Iterating Sieve mailbox attributes");

	ret = mail_sieve_user_init(user, &svstorage);
	if (ret <= 0) {
		if (ret < 0)
			mail_storage_set_internal_error(siter->iter.box->storage);
		return ret;
	}

	if (sieve_storage_list_init(svstorage, &siter->sieve_list) < 0) {
		mail_storage_set_critical(
			siter->iter.box->storage,
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
	    type == MAIL_ATTRIBUTE_TYPE_PRIVATE) {
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

	if (siter->sieve_list == NULL)
		return NULL;

	/* Iterate through all scripts in sieve_dir */
	while ((scriptname = sieve_storage_list_next(siter->sieve_list,
						     &active)) != NULL) {
		if (active)
			siter->have_active = TRUE;
		str_truncate(siter->name,
			     strlen(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES));
		str_append(siter->name, scriptname);
		return str_c(siter->name);
	}
	if (sieve_storage_list_deinit(&siter->sieve_list) < 0) {
		mail_storage_set_critical(
			siter->iter.box->storage,
			"Failed to iterate sieve scripts: %s",
			sieve_storage_get_last_error(svstorage, NULL));
		siter->failed = TRUE;
		return NULL;
	}

	/* Check whether active script is a proper symlink or a regular file */
	ret = sieve_storage_is_singular(svstorage);
	if (ret < 0) {
		mail_storage_set_critical(
			siter->iter.box->storage,
			"Failed to iterate sieve scripts: %s",
			sieve_storage_get_last_error(svstorage, NULL));
		return NULL;
	}

	/* Regular file */
	if (ret > 0)
		return MAILBOX_ATTRIBUTE_SIEVE_DEFAULT;

	/* Symlink or none active */
	return (siter->have_active ? MAILBOX_ATTRIBUTE_SIEVE_DEFAULT : NULL);
}

static const char *
sieve_attribute_iter_next(struct mailbox_attribute_iter *iter)
{
	struct sieve_mailbox_attribute_iter *siter =
		(struct sieve_mailbox_attribute_iter *)iter;
	union mailbox_module_context *sbox = SIEVE_MAIL_CONTEXT(iter->box);
	struct mail_user *user = iter->box->storage->user;
	struct sieve_mail_user *suser = SIEVE_USER_CONTEXT(user);
	const char *key;

	if (siter->sieve_list != NULL) {
		key = sieve_attribute_iter_next_script(siter);
		if (key != NULL) {
			e_debug(suser->event,
				"Iterating Sieve mailbox attribute: %s", key);
			return key;
		}
	}
	return sbox->super.attribute_iter_next(siter->super);
}

static int sieve_attribute_iter_deinit(struct mailbox_attribute_iter *iter)
{
	struct sieve_mailbox_attribute_iter *siter =
		(struct sieve_mailbox_attribute_iter *)iter;
	union mailbox_module_context *sbox = SIEVE_MAIL_CONTEXT(iter->box);
	int ret = siter->failed ? -1 : 0;

	if (siter->super != NULL) {
		if (sbox->super.attribute_iter_deinit(siter->super) < 0)
			ret = -1;
	}
	(void)sieve_storage_list_deinit(&siter->sieve_list);
	str_free(&siter->name);
	i_free(siter);
	return ret;
}

static void sieve_mail_user_created(struct mail_user *user)
{
	struct sieve_mail_user *suser;
	struct mail_user_vfuncs *v = user->vlast;

	suser = p_new(user->pool, struct sieve_mail_user, 1);
	suser->module_ctx.super = *v;
	user->vlast = &suser->module_ctx.super;
	v->deinit = mail_sieve_user_deinit;
	MODULE_CONTEXT_SET(user, sieve_user_module, suser);

	suser->event = event_create(user->event);
	event_set_append_log_prefix(suser->event, "doveadm-sieve: ");
}

static void sieve_mailbox_allocated(struct mailbox *box)
{
	struct mailbox_vfuncs *v = box->vlast;
	union mailbox_module_context *sbox;

	/* Attribute syncing is done via INBOX */
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
	.mail_user_created = sieve_mail_user_created,
	.mailbox_allocated = sieve_mailbox_allocated
};

void doveadm_sieve_sync_init(struct module *module)
{
	mail_storage_hooks_add_forced(module,
				      &doveadm_sieve_mail_storage_hooks);
}
