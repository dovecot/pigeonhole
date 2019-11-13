/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"

#include "sieve.h"
#include "sieve-script.h"
#include "sieve-storage.h"

#include "managesieve-common.h"
#include "managesieve-commands.h"

static void
cmd_setactive_activate(struct client_command_context *cmd,
		       const char *scriptname)
{
	struct client *client = cmd->client;
	struct sieve_storage *storage = client->storage;
	struct sieve_script *script;
	string_t *errors = NULL;
	const char *errormsg = NULL;
	unsigned int warning_count = 0, error_count = 0;
	bool success = TRUE;
	int ret;

	event_add_str(cmd->event, "script_name", scriptname);

	script = sieve_storage_open_script(storage, scriptname, NULL);
	if (script == NULL) {
		client_command_storage_error(
			cmd, "Failed to open script `%s' for activation",
			scriptname);
		return;
	}

	if (sieve_script_is_active(script) <= 0) T_BEGIN {
		/* Script is first being activated; compile it again without the
		   UPLOAD flag. */
		struct sieve_error_handler *ehandler;
		enum sieve_compile_flags cpflags =
			SIEVE_COMPILE_FLAG_NOGLOBAL |
			SIEVE_COMPILE_FLAG_ACTIVATED;
		struct sieve_binary *sbin;
		enum sieve_error error;

		/* Prepare error handler */
		errors = str_new(default_pool, 1024);
		ehandler = sieve_strbuf_ehandler_create(
			client->svinst, errors, TRUE,
			client->set->managesieve_max_compile_errors);

		/* Compile */
		sbin = sieve_compile_script(script, ehandler, cpflags, &error);
		if (sbin == NULL) {
			if (error != SIEVE_ERROR_NOT_VALID) {
				errormsg = sieve_script_get_last_error(
					script, &error);
				if (error == SIEVE_ERROR_NONE)
					errormsg = NULL;
			}
			success = FALSE;
		} else {
			sieve_close(&sbin);
		}

		warning_count = sieve_get_warnings(ehandler);
		error_count = sieve_get_errors(ehandler);
		sieve_error_handler_unref(&ehandler);
	} T_END;

	/* Activate only when script is valid (or already active) */
	if (success) {
		/* Refresh activation no matter what; this can also
		   resolve some erroneous situations. */
		ret = sieve_script_activate(script, (time_t)-1);
		if (ret < 0) {
			client_command_storage_error(
				cmd, "Failed to activate script `%s'",
				scriptname);
		} else {
			struct event_passthrough *e =
				client_command_create_finish_event(cmd)->
				add_int("compile_warnings", warning_count);
			e_debug(e->event(), "Activated script `%s' "
				" (%u warnings%s)",
				scriptname, warning_count,
				(ret == 0 ? ", redundant" : ""));

			if (warning_count > 0) {
				client_send_okresp(
					client, "WARNINGS",
					str_c(errors));
			} else {
				client_send_ok(client,
					(ret > 0 ?
					 "Setactive completed." :
					 "Script is already active."));
			}
		}
	} else if (errormsg == NULL) {
		struct event_passthrough *e =
			client_command_create_finish_event(cmd)->
			add_str("error", "Compilation failed")->
			add_int("compile_errors", error_count)->
			add_int("compile_warnings", warning_count);
		e_debug(e->event(), "Failed to activate script `%s': "
			"Compilation failed (%u errors, %u warnings)",
			scriptname, error_count, warning_count);

		client_send_no(client, str_c(errors));
	} else {
		struct event_passthrough *e =
			client_command_create_finish_event(cmd)->
			add_str("error", errormsg);
		e_debug(e->event(), "Failed to activate script `%s': %s",
			scriptname, errormsg);

		client_send_no(client, errormsg);
	}

	if (errors != NULL)
		str_free(&errors);
	sieve_script_unref(&script);
}

static void
cmd_setactive_deactivate(struct client_command_context *cmd)
{
	struct client *client = cmd->client;
	struct sieve_storage *storage = client->storage;
	int ret;

	ret = sieve_storage_deactivate(storage, (time_t)-1);
	if (ret < 0) {
		client_command_storage_error(
			cmd, "Failed to deactivate script");
		return;
	}

	struct event_passthrough *e =
		client_command_create_finish_event(cmd);
	e_debug(e->event(), "Deactivated script");

	client_send_ok(client, (ret > 0 ?
				"Active script is now deactivated." :
				"No scripts currently active."));
}

bool cmd_setactive(struct client_command_context *cmd)
{
	const char *scriptname;

	/* <scriptname> */
	if (!client_read_string_args(cmd, TRUE, 1, &scriptname))
		return FALSE;

	/* Activate, or deactivate */
	if (*scriptname != '\0')
		cmd_setactive_activate(cmd, scriptname);
	else
		cmd_setactive_deactivate(cmd);

	return TRUE;
}
