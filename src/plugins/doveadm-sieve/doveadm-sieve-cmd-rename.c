/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "doveadm-mail.h"

#include "sieve.h"
#include "sieve-script.h"
#include "sieve-storage.h"

#include "doveadm-sieve-cmd.h"

struct doveadm_sieve_rename_cmd_context {
	struct doveadm_sieve_cmd_context ctx;

	const char *oldname, *newname;
};

static int cmd_sieve_rename_run(struct doveadm_sieve_cmd_context *_ctx)
{
	struct doveadm_sieve_rename_cmd_context *ctx =
		container_of(_ctx, struct doveadm_sieve_rename_cmd_context,
			     ctx);
	struct event *event = _ctx->ctx.cctx->event;
	struct sieve_storage *storage = _ctx->storage;
	struct sieve_script *script;
	enum sieve_error error_code;
	int ret = 0;

	if (sieve_storage_open_script(storage, ctx->oldname,
				      &script, NULL) < 0) {
		e_error(event, "Failed to rename Sieve script: %s",
			sieve_storage_get_last_error(storage, &error_code));
		doveadm_sieve_cmd_failed_error(_ctx, error_code);
		ret = -1;
	} else if (sieve_script_rename(script, ctx->newname) < 0) {
		e_error(event, "Failed to rename Sieve script: %s",
			sieve_storage_get_last_error(storage, &error_code));
		doveadm_sieve_cmd_failed_error(_ctx, error_code);
		ret = -1;
	}

	sieve_script_unref(&script);
	return ret;
}

static void cmd_sieve_rename_init(struct doveadm_mail_cmd_context *_ctx)
{
	struct doveadm_cmd_context *cctx = _ctx->cctx;
	struct doveadm_sieve_rename_cmd_context *ctx =
		container_of(_ctx, struct doveadm_sieve_rename_cmd_context,
			     ctx.ctx);

	if (!doveadm_cmd_param_str(cctx, "oldname", &ctx->oldname) ||
	    !doveadm_cmd_param_str(cctx, "newname", &ctx->newname))
		doveadm_mail_help_name("sieve rename");
	doveadm_sieve_cmd_scriptname_check(ctx->oldname);
	doveadm_sieve_cmd_scriptname_check(ctx->newname);
}

static struct doveadm_mail_cmd_context *cmd_sieve_rename_alloc(void)
{
	struct doveadm_sieve_rename_cmd_context *ctx;

	ctx = doveadm_sieve_cmd_alloc(struct doveadm_sieve_rename_cmd_context);
	ctx->ctx.ctx.v.init = cmd_sieve_rename_init;
	ctx->ctx.v.run = cmd_sieve_rename_run;
	return &ctx->ctx.ctx;
}

struct doveadm_cmd_ver2 doveadm_sieve_cmd_rename = {
	.name = "sieve rename",
	.mail_cmd = cmd_sieve_rename_alloc,
	.usage = DOVEADM_CMD_MAIL_USAGE_PREFIX"<oldname> <newname>",
DOVEADM_CMD_PARAMS_START
DOVEADM_CMD_MAIL_COMMON
DOVEADM_CMD_PARAM('\0',"oldname",CMD_PARAM_STR,CMD_PARAM_FLAG_POSITIONAL)
DOVEADM_CMD_PARAM('\0',"newname",CMD_PARAM_STR,CMD_PARAM_FLAG_POSITIONAL)
DOVEADM_CMD_PARAMS_END
};
