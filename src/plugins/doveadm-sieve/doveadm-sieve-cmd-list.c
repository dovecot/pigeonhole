/* Copyright (c) 2002-2016 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "doveadm-print.h"
#include "doveadm-mail.h"

#include "sieve.h"
#include "sieve-storage.h"

#include "doveadm-sieve-cmd.h"

static int
cmd_sieve_list_run(struct doveadm_sieve_cmd_context *_ctx)
{
	struct sieve_storage *storage = _ctx->storage;
	struct sieve_storage_list_context *lctx;
	enum sieve_error error;
	const char *scriptname;
	bool active;

	if ( (lctx = sieve_storage_list_init(storage))
		== NULL ) {
		i_error("Listing Sieve scripts failed: %s",
			sieve_storage_get_last_error(storage, &error));
		doveadm_sieve_cmd_failed_error(_ctx, error);
		return -1;
	}

	while ( (scriptname=sieve_storage_list_next(lctx, &active))
		!= NULL ) {
		doveadm_print(scriptname);
		if ( active )
			doveadm_print("ACTIVE");
		else
			doveadm_print("");
	}

	if ( sieve_storage_list_deinit(&lctx) < 0 ) {
		i_error("Listing Sieve scripts failed: %s",
			sieve_storage_get_last_error(storage, &error));
		doveadm_sieve_cmd_failed_error(_ctx, error);
		return -1;
	}
	return 0;
}

static void cmd_sieve_list_init
(struct doveadm_mail_cmd_context *_ctx ATTR_UNUSED,
	const char *const args[] ATTR_UNUSED)
{
	doveadm_print_header("script", "script",
		DOVEADM_PRINT_HEADER_FLAG_HIDE_TITLE);
	doveadm_print_header("active", "active",
		DOVEADM_PRINT_HEADER_FLAG_HIDE_TITLE);
}

static struct doveadm_mail_cmd_context *
cmd_sieve_list_alloc(void)
{
	struct doveadm_sieve_cmd_context *ctx;

	ctx = doveadm_sieve_cmd_alloc(struct doveadm_sieve_cmd_context);
	ctx->ctx.v.init = cmd_sieve_list_init;
	ctx->ctx.getopt_args = "s";
	ctx->v.run = cmd_sieve_list_run;
	doveadm_print_init(DOVEADM_PRINT_TYPE_FLOW);
	return &ctx->ctx;
}

struct doveadm_cmd_ver2 doveadm_sieve_cmd_list = {
	.name = "sieve list",
	.mail_cmd = cmd_sieve_list_alloc,
	.usage = DOVEADM_CMD_MAIL_USAGE_PREFIX,
DOVEADM_CMD_PARAMS_START
DOVEADM_CMD_MAIL_COMMON
DOVEADM_CMD_PARAMS_END
};

