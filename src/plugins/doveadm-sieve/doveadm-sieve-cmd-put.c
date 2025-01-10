/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "istream.h"
#include "doveadm-mail.h"

#include "sieve.h"
#include "sieve-script.h"
#include "sieve-storage.h"

#include "doveadm-sieve-cmd.h"

struct doveadm_sieve_put_cmd_context {
	struct doveadm_sieve_cmd_context ctx;

	const char *scriptname;

	bool activate:1;
};

static int cmd_sieve_put_run(struct doveadm_sieve_cmd_context *_ctx)
{
	struct doveadm_sieve_put_cmd_context *ctx =
		container_of(_ctx, struct doveadm_sieve_put_cmd_context, ctx);
	struct event *event = _ctx->ctx.cctx->event;
	struct sieve_storage_save_context *save_ctx;
	struct sieve_storage *storage = _ctx->storage;
	struct istream *input = _ctx->ctx.cmd_input;
	enum sieve_error error_code;
	ssize_t ret;
	bool save_failed = FALSE;

	save_ctx = sieve_storage_save_init(storage, ctx->scriptname, input);
	if (save_ctx == NULL) {
		e_error(event, "Saving failed: %s",
			sieve_storage_get_last_error(storage, &error_code));
		doveadm_sieve_cmd_failed_error(_ctx, error_code);
		return -1;
	}

	while ((ret = i_stream_read(input)) > 0 || ret == -2) {
		if (sieve_storage_save_continue(save_ctx) < 0) {
			save_failed = TRUE;
			ret = -1;
			break;
		}
	}
	i_assert(ret == -1);

	if (input->stream_errno != 0) {
		e_error(event, "read(script input) failed: %s",
			i_stream_get_error(input));
		doveadm_sieve_cmd_failed_error(&ctx->ctx,
					       SIEVE_ERROR_TEMP_FAILURE);
	} else if (save_failed) {
		e_error(event, "Saving failed: %s",
			sieve_storage_get_last_error(storage, NULL));
		doveadm_sieve_cmd_failed_storage(&ctx->ctx, storage);
	} else if (sieve_storage_save_finish(save_ctx) < 0) {
		e_error(event, "Saving failed: %s",
			sieve_storage_get_last_error(storage, NULL));
		doveadm_sieve_cmd_failed_storage(&ctx->ctx, storage);
	} else {
		ret = 0;
	}

	/* Verify that script compiles */
	if (ret == 0) {
		struct sieve_error_handler *ehandler;
		enum sieve_compile_flags cpflags =
			SIEVE_COMPILE_FLAG_NOGLOBAL |
			SIEVE_COMPILE_FLAG_UPLOADED;
		struct sieve_script *script;
		struct sieve_binary *sbin;

		/* Obtain script object for uploaded script */
		script = sieve_storage_save_get_tempscript(save_ctx);

		/* Check result */
		if (script == NULL) {
			e_error(event, "Saving failed: %s",
				sieve_storage_get_last_error(storage,
							     &error_code));
			doveadm_sieve_cmd_failed_error(_ctx, error_code);
			ret = -1;

		} else {
			/* Mark this as an activation when we are replacing the
			   active script.
			 */
			if (ctx->activate ||
			    sieve_storage_save_will_activate(save_ctx))
				cpflags |= SIEVE_COMPILE_FLAG_ACTIVATED;

			/* Compile */
			ehandler = sieve_master_ehandler_create(
				ctx->ctx.svinst, 0);
			if (sieve_compile_script(script, ehandler, cpflags,
						 &sbin, &error_code) < 0) {
				doveadm_sieve_cmd_failed_error(
					_ctx, error_code);
				ret = -1;
			} else {
				sieve_close(&sbin);

				/* Script is valid; commit it to storage */
				ret = sieve_storage_save_commit(&save_ctx);
				if (ret < 0) {
					e_error(event, "Saving failed: %s",
						sieve_storage_get_last_error(
							storage, &error_code));
					doveadm_sieve_cmd_failed_error(
						_ctx, error_code);
					ret = -1;
				}
			}
			sieve_error_handler_unref(&ehandler);
		}
	}

	if (save_ctx != NULL)
		sieve_storage_save_cancel(&save_ctx);

	if (ctx->activate && ret == 0) {
		struct sieve_script *script;

		if (sieve_storage_open_script(storage, ctx->scriptname,
					      &script, NULL) < 0 ||
		    sieve_script_activate(script, (time_t)-1) < 0) {
			e_error(event, "Failed to activate Sieve script: %s",
				sieve_storage_get_last_error(storage,
							     &error_code));
			doveadm_sieve_cmd_failed_error(_ctx, error_code);
			ret = -1;
		}
		sieve_script_unref(&script);
	}

	i_assert(input->eof);
	return ret < 0 ? -1 : 0;
}

static void cmd_sieve_put_init(struct doveadm_mail_cmd_context *_ctx)
{
	struct doveadm_cmd_context *cctx = _ctx->cctx;
	struct doveadm_sieve_put_cmd_context *ctx =
		container_of(_ctx, struct doveadm_sieve_put_cmd_context,
			     ctx.ctx);

	ctx->activate = doveadm_cmd_param_flag(cctx, "activate");
	if (!doveadm_cmd_param_str(cctx, "scriptname", &ctx->scriptname))
		doveadm_mail_help_name("sieve put");
	doveadm_sieve_cmd_scriptname_check(ctx->scriptname);
	doveadm_mail_get_input(_ctx);
}

static struct doveadm_mail_cmd_context *
cmd_sieve_put_alloc(void)
{
	struct doveadm_sieve_put_cmd_context *ctx;

	ctx = doveadm_sieve_cmd_alloc(struct doveadm_sieve_put_cmd_context);
	ctx->ctx.ctx.v.init = cmd_sieve_put_init;
	ctx->ctx.v.run = cmd_sieve_put_run;
	return &ctx->ctx.ctx;
}

struct doveadm_cmd_ver2 doveadm_sieve_cmd_put = {
	.name = "sieve put",
	.mail_cmd = cmd_sieve_put_alloc,
	.usage = DOVEADM_CMD_MAIL_USAGE_PREFIX"[-a] <scriptname>",
DOVEADM_CMD_PARAMS_START
DOVEADM_CMD_MAIL_COMMON
DOVEADM_CMD_PARAM('a',"activate",CMD_PARAM_BOOL,0)
DOVEADM_CMD_PARAM('\0',"scriptname",CMD_PARAM_STR,CMD_PARAM_FLAG_POSITIONAL)
DOVEADM_CMD_PARAM('\0',"file",CMD_PARAM_ISTREAM,CMD_PARAM_FLAG_POSITIONAL)
DOVEADM_CMD_PARAMS_END
};
