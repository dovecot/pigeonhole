/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"
#include "doveadm-mail.h"

#include "sieve.h"
#include "sieve-script.h"
#include "sieve-storage.h"

#include "doveadm-sieve-cmd.h"

struct doveadm_sieve_delete_cmd_context {
	struct doveadm_sieve_cmd_context ctx;

	ARRAY_TYPE(const_string) scriptnames;
	bool ignore_active:1;
};

static int cmd_sieve_delete_run(struct doveadm_sieve_cmd_context *_ctx)
{
	struct doveadm_sieve_delete_cmd_context *ctx =
		container_of(_ctx, struct doveadm_sieve_delete_cmd_context,
			     ctx);
	struct event *event = _ctx->ctx.cctx->event;
	struct sieve_storage *storage = _ctx->storage;
	const ARRAY_TYPE(const_string) *scriptnames = &ctx->scriptnames;
	const char *scriptname;
	struct sieve_script *script;
	enum sieve_error error_code;
	int ret = 0;

	array_foreach_elem(scriptnames, scriptname) {
		int sret = 0;

		sret = sieve_storage_open_script(storage, scriptname,
						 &script, NULL);
		if (sret == 0) {
			if (sieve_script_delete(script,
						ctx->ignore_active) < 0) {
				(void)sieve_storage_get_last_error(
					storage, &error_code);
				sret = -1;
			}
			sieve_script_unref(&script);
		}

		if (sret < 0) {
			e_error(event, "Failed to delete Sieve script: %s",
				sieve_storage_get_last_error(storage,
							     &error_code));
			doveadm_sieve_cmd_failed_error(_ctx, error_code);
			ret = -1;
		}
	}
	return ret;
}

static void cmd_sieve_delete_init(struct doveadm_mail_cmd_context *_ctx)
{
	struct doveadm_cmd_context *cctx = _ctx->cctx;
	struct doveadm_sieve_delete_cmd_context *ctx =
		container_of(_ctx, struct doveadm_sieve_delete_cmd_context,
			     ctx.ctx);

	ctx->ignore_active = doveadm_cmd_param_flag(cctx, "ignore-active");

	if (!doveadm_cmd_param_array_append(cctx, "scriptname",
					    &ctx->scriptnames))
		doveadm_mail_help_name("sieve delete");

	doveadm_sieve_cmd_scriptnames_check(&ctx->scriptnames);
}

static struct doveadm_mail_cmd_context *cmd_sieve_delete_alloc(void)
{
	struct doveadm_sieve_delete_cmd_context *ctx;

	ctx = doveadm_sieve_cmd_alloc(struct doveadm_sieve_delete_cmd_context);
	ctx->ctx.ctx.v.init = cmd_sieve_delete_init;
	ctx->ctx.v.run = cmd_sieve_delete_run;
	p_array_init(&ctx->scriptnames, ctx->ctx.ctx.pool, 16);
	return &ctx->ctx.ctx;
}

struct doveadm_cmd_ver2 doveadm_sieve_cmd_delete = {
	.name = "sieve delete",
	.mail_cmd = cmd_sieve_delete_alloc,
	.usage = DOVEADM_CMD_MAIL_USAGE_PREFIX"[-a] <scriptname> [...]",
DOVEADM_CMD_PARAMS_START
DOVEADM_CMD_MAIL_COMMON
DOVEADM_CMD_PARAM('a',"ignore-active",CMD_PARAM_BOOL,0)
DOVEADM_CMD_PARAM('\0',"scriptname",CMD_PARAM_ARRAY,CMD_PARAM_FLAG_POSITIONAL)
DOVEADM_CMD_PARAMS_END
};
