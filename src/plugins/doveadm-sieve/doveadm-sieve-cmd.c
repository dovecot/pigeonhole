/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "unichar.h"
#include "mail-storage.h"
#include "doveadm-mail.h"

#include "sieve.h"
#include "sieve-script.h"
#include "sieve-storage.h"

#include "doveadm-sieve-cmd.h"

void doveadm_sieve_cmd_failed_error(struct doveadm_sieve_cmd_context *ctx,
				    enum sieve_error error_code)
{
	struct doveadm_mail_cmd_context *mctx = &ctx->ctx;
	int exit_code = 0;

	switch (error_code) {
	case SIEVE_ERROR_NONE:
		i_unreached();
		return;
	case SIEVE_ERROR_TEMP_FAILURE:
		exit_code = EX_TEMPFAIL;
		break;
	case SIEVE_ERROR_NOT_POSSIBLE:
	case SIEVE_ERROR_EXISTS:
	case SIEVE_ERROR_NOT_VALID:
	case SIEVE_ERROR_ACTIVE:
		exit_code = DOVEADM_EX_NOTPOSSIBLE;
		break;
	case SIEVE_ERROR_BAD_PARAMS:
		exit_code = EX_USAGE;
		break;
	case SIEVE_ERROR_NO_PERMISSION:
		exit_code = EX_NOPERM;
		break;
	case SIEVE_ERROR_NO_QUOTA:
		exit_code = EX_CANTCREAT;
		break;
	case SIEVE_ERROR_NOT_FOUND:
		exit_code = DOVEADM_EX_NOTFOUND;
		break;
	default:
		i_unreached();
	}
	/* Tempfail overrides all other exit codes, otherwise use whatever
	   error happened first */
	if (mctx->exit_code == 0 || exit_code == EX_TEMPFAIL)
		mctx->exit_code = exit_code;
}

void doveadm_sieve_cmd_failed_storage(struct doveadm_sieve_cmd_context *ctx,
				      struct sieve_storage *storage)
{
	enum sieve_error error_code;

	(void)sieve_storage_get_last_error(storage, &error_code);
	doveadm_sieve_cmd_failed_error(ctx, error_code);
}

static const char *
doveadm_sieve_cmd_get_setting(struct sieve_instance *svinst ATTR_UNUSED,
			      void *context, const char *identifier)
{
	struct doveadm_sieve_cmd_context *ctx =
		(struct doveadm_sieve_cmd_context *)context;

	return mail_user_plugin_getenv(ctx->ctx.cur_mail_user, identifier);
}

static const struct sieve_callbacks sieve_callbacks = {
	NULL,
	doveadm_sieve_cmd_get_setting,
};

void doveadm_sieve_cmd_scriptname_check(const char *arg)
{
	if (!uni_utf8_str_is_valid(arg))
		i_fatal_status(EX_DATAERR,
			"Sieve script name not valid UTF-8: %s", arg);

	if (!sieve_script_name_is_valid(arg) )
		i_fatal_status(EX_DATAERR,
			"Sieve script name not valid: %s", arg);

}

void doveadm_sieve_cmd_scriptnames_check(ARRAY_TYPE(const_string) *args)
{
	const char *const *arg;
	array_foreach(args, arg)
		doveadm_sieve_cmd_scriptname_check(*arg);
}

static int
doveadm_sieve_cmd_run(struct doveadm_mail_cmd_context *_ctx,
		      struct mail_user *user)
{
	struct doveadm_sieve_cmd_context *ctx =
		container_of(_ctx, struct doveadm_sieve_cmd_context, ctx);
	struct event *event = _ctx->cctx->event;

	struct sieve_environment svenv;
	enum sieve_error error_code;
	int ret;

	i_zero(&svenv);
	svenv.username = user->username;
	(void)mail_user_get_home(user, &svenv.home_dir);
	svenv.base_dir = user->set->base_dir;
	svenv.flags = SIEVE_FLAG_HOME_RELATIVE;

	if (sieve_init(&svenv, &sieve_callbacks, ctx,
		       user->set->mail_debug, &ctx->svinst) < 0)
		return -1;

	if (sieve_storage_create_personal(ctx->svinst, user,
					  SIEVE_SCRIPT_CAUSE_ANY,
					  SIEVE_STORAGE_FLAG_READWRITE,
					  &ctx->storage, &error_code) < 0) {
		switch (error_code) {
		case SIEVE_ERROR_NOT_POSSIBLE:
			error_code = SIEVE_ERROR_NOT_FOUND;
			e_error(event, "Failed to open Sieve storage: "
				"Sieve is disabled for this user");
			break;
		case SIEVE_ERROR_NOT_FOUND:
			e_error(event, "Failed to open Sieve storage: "
				"User cannot manage personal Sieve scripts.");
			break;
		default:
			e_error(event, "Failed to open Sieve storage.");
		}
		doveadm_sieve_cmd_failed_error(ctx, error_code);
		ret =  -1;
	} else {
		i_assert(ctx->v.run != NULL);
		ret = ctx->v.run(ctx);
		sieve_storage_unref(&ctx->storage);
	}

	sieve_deinit(&ctx->svinst);
	return ret;
}

struct doveadm_sieve_cmd_context *doveadm_sieve_cmd_alloc_size(size_t size)
{
	struct doveadm_sieve_cmd_context *ctx;

	ctx = (struct doveadm_sieve_cmd_context *)
		doveadm_mail_cmd_alloc_size(size);
	ctx->ctx.v.run = doveadm_sieve_cmd_run;
	return ctx;
}

static struct doveadm_cmd_ver2 *doveadm_sieve_commands[] = {
	&doveadm_sieve_cmd_list,
	&doveadm_sieve_cmd_get,
	&doveadm_sieve_cmd_put,
	&doveadm_sieve_cmd_delete,
	&doveadm_sieve_cmd_activate,
	&doveadm_sieve_cmd_deactivate,
	&doveadm_sieve_cmd_rename,
};

void doveadm_sieve_cmds_init(void)
{
	unsigned int i;

	for (i = 0; i < N_ELEMENTS(doveadm_sieve_commands); i++)
		doveadm_cmd_register_ver2(doveadm_sieve_commands[i]);
}
