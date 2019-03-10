/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "doveadm-mail.h"

#include "sieve.h"
#include "sieve-script.h"
#include "sieve-storage.h"

#include "doveadm-sieve-cmd.h"

struct doveadm_sieve_activate_cmd_context {
	struct doveadm_sieve_cmd_context ctx;

	const char *scriptname;
};

static int
cmd_sieve_activate_run(struct doveadm_sieve_cmd_context *_ctx)
{
	struct doveadm_sieve_activate_cmd_context *ctx =
		(struct doveadm_sieve_activate_cmd_context *)_ctx;
	struct sieve_storage *storage = _ctx->storage;
	struct sieve_script *script;
	enum sieve_error error;
	int ret = 0;

	script = sieve_storage_open_script
		(storage, ctx->scriptname, NULL);
	if ( script == NULL ) {
		i_error("Failed to activate Sieve script: %s",
			sieve_storage_get_last_error(storage, &error));
		doveadm_sieve_cmd_failed_error(_ctx, error);
		return -1;
	}

	if ( sieve_script_is_active(script) <= 0 ) {
		/* Script is first being activated; compile it again without the UPLOAD
		 * flag.
		 */
		struct sieve_error_handler *ehandler;
		enum sieve_compile_flags cpflags =
			SIEVE_COMPILE_FLAG_NOGLOBAL | SIEVE_COMPILE_FLAG_ACTIVATED;
		struct sieve_binary *sbin;
		enum sieve_error error;

		/* Compile */
		ehandler = sieve_master_ehandler_create(ctx->ctx.svinst, 0);
		if ( (sbin=sieve_compile_script
			(script, ehandler, cpflags, &error)) == NULL ) {
			doveadm_sieve_cmd_failed_error(_ctx, error);
			ret = -1;
		} else {
			sieve_close(&sbin);
		}
		sieve_error_handler_unref(&ehandler);
	}

	/* Activate only when script is valid (or already active) */
	if ( ret == 0 ) {
		/* Refresh activation no matter what; this can also resolve some erroneous
		 * situations.
		 */
		ret = sieve_script_activate(script, (time_t)-1);
		if ( ret < 0 ) {
			i_error("Failed to activate Sieve script: %s",
				sieve_storage_get_last_error(storage, &error));
			doveadm_sieve_cmd_failed_error(_ctx, error);
			ret = -1;
		}
	}

	sieve_script_unref(&script);
	return ret;
}

static int cmd_sieve_deactivate_run
(struct doveadm_sieve_cmd_context *_ctx)
{
	struct sieve_storage *storage = _ctx->storage;
	enum sieve_error error;

	if (sieve_storage_deactivate(storage, (time_t)-1) < 0) {
		i_error("Failed to deactivate Sieve script: %s",
			sieve_storage_get_last_error(storage, &error));
		doveadm_sieve_cmd_failed_error(_ctx, error);
		return -1;
	}
	return 0;
}

static void cmd_sieve_activate_init
(struct doveadm_mail_cmd_context *_ctx,
	const char *const args[])
{
	struct doveadm_sieve_activate_cmd_context *ctx =
		(struct doveadm_sieve_activate_cmd_context *)_ctx;

	if (str_array_length(args) != 1)
		doveadm_mail_help_name("sieve activate");
	doveadm_sieve_cmd_scriptnames_check(args);

	ctx->scriptname = p_strdup(ctx->ctx.ctx.pool, args[0]);
}

static struct doveadm_mail_cmd_context *
cmd_sieve_activate_alloc(void)
{
	struct doveadm_sieve_activate_cmd_context *ctx;

	ctx = doveadm_sieve_cmd_alloc(struct doveadm_sieve_activate_cmd_context);
	ctx->ctx.ctx.v.init = cmd_sieve_activate_init;
	ctx->ctx.v.run = cmd_sieve_activate_run;
	return &ctx->ctx.ctx;
}

static struct doveadm_mail_cmd_context *
cmd_sieve_deactivate_alloc(void)
{
	struct doveadm_sieve_cmd_context *ctx;

	ctx = doveadm_sieve_cmd_alloc(struct doveadm_sieve_cmd_context);
	ctx->v.run = cmd_sieve_deactivate_run;
	return &ctx->ctx;
}

struct doveadm_cmd_ver2 doveadm_sieve_cmd_activate = {
	.name = "sieve activate",
	.mail_cmd = cmd_sieve_activate_alloc,
	.usage = DOVEADM_CMD_MAIL_USAGE_PREFIX"<scriptname>",
DOVEADM_CMD_PARAMS_START
DOVEADM_CMD_MAIL_COMMON
DOVEADM_CMD_PARAM('\0',"scriptname",CMD_PARAM_STR,CMD_PARAM_FLAG_POSITIONAL)
DOVEADM_CMD_PARAMS_END
};

struct doveadm_cmd_ver2 doveadm_sieve_cmd_deactivate = {
	.name = "sieve deactivate",
	.mail_cmd = cmd_sieve_deactivate_alloc,
	.usage = DOVEADM_CMD_MAIL_USAGE_PREFIX,
DOVEADM_CMD_PARAMS_START
DOVEADM_CMD_MAIL_COMMON
DOVEADM_CMD_PARAMS_END
};
