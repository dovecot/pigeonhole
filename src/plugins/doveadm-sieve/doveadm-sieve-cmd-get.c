/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "doveadm-print.h"
#include "doveadm-mail.h"

#include "sieve.h"
#include "sieve-script.h"
#include "sieve-storage.h"

#include "doveadm-sieve-cmd.h"

struct doveadm_sieve_get_cmd_context {
	struct doveadm_sieve_cmd_context ctx;

	const char *scriptname;
};

static int cmd_sieve_get_run(struct doveadm_sieve_cmd_context *_ctx)
{
	struct doveadm_sieve_get_cmd_context *ctx =
		container_of(_ctx, struct doveadm_sieve_get_cmd_context, ctx);

	struct sieve_script *script;
	struct istream *input;
	enum sieve_error error;
	int ret;

	script = sieve_storage_open_script(_ctx->storage, ctx->scriptname,
					  &error);
	if (script == NULL ||
	    sieve_script_get_stream(script, &input, &error) < 0 ) {
		i_error("Failed to open Sieve script: %s",
			sieve_storage_get_last_error(_ctx->storage, &error));
		doveadm_sieve_cmd_failed_error(_ctx, error);
		if (script != NULL)
			sieve_script_unref(&script);
		return -1;
	}

	ret = doveadm_print_istream(input);
	sieve_script_unref(&script);
	return ret;
}

static void cmd_sieve_get_init(struct doveadm_mail_cmd_context *_ctx)
{
	struct doveadm_cmd_context *cctx = _ctx->cctx;
	struct doveadm_sieve_get_cmd_context *ctx =
		container_of(_ctx, struct doveadm_sieve_get_cmd_context, ctx.ctx);

	if (!doveadm_cmd_param_str(cctx, "scriptname", &ctx->scriptname))
		doveadm_mail_help_name("sieve get");

	doveadm_sieve_cmd_scriptname_check(ctx->scriptname);
	doveadm_print_header_simple("sieve script");
}

static struct doveadm_mail_cmd_context *cmd_sieve_get_alloc(void)
{
	struct doveadm_sieve_get_cmd_context *ctx;

	ctx = doveadm_sieve_cmd_alloc(struct doveadm_sieve_get_cmd_context);
	ctx->ctx.ctx.v.init = cmd_sieve_get_init;
	ctx->ctx.v.run = cmd_sieve_get_run;
	doveadm_print_init("pager");
	return &ctx->ctx.ctx;
}

struct doveadm_cmd_ver2 doveadm_sieve_cmd_get = {
	.name = "sieve get",
	.mail_cmd = cmd_sieve_get_alloc,
	.usage = DOVEADM_CMD_MAIL_USAGE_PREFIX"<scriptname>",
DOVEADM_CMD_PARAMS_START
DOVEADM_CMD_MAIL_COMMON
DOVEADM_CMD_PARAM('\0',"scriptname",CMD_PARAM_STR,CMD_PARAM_FLAG_POSITIONAL)
DOVEADM_CMD_PARAMS_END
};
