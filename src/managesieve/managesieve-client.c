/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "ioloop.h"
#include "llist.h"
#include "str.h"
#include "hostpid.h"
#include "net.h"
#include "istream.h"
#include "ostream.h"
#include "iostream.h"
#include "iostream-rawlog.h"
#include "var-expand.h"
#include "time-util.h"
#include "master-service.h"
#include "mail-storage-service.h"
#include "mail-namespace.h"

#include "sieve.h"
#include "sieve-storage.h"

#include "managesieve-quote.h"
#include "managesieve-common.h"
#include "managesieve-commands.h"
#include "managesieve-client.h"

#include <unistd.h>

extern struct mail_storage_callbacks mail_storage_callbacks;
struct managesieve_module_register managesieve_module_register = { 0 };

struct client *managesieve_clients = NULL;
unsigned int managesieve_client_count = 0;

static const char *
managesieve_sieve_get_setting(void *context, const char *identifier)
{
	struct mail_user *mail_user = (struct mail_user *) context;

	if (mail_user == NULL)
		return NULL;

	return mail_user_plugin_getenv(mail_user, identifier);
}

static const struct sieve_callbacks managesieve_sieve_callbacks = {
	NULL,
	managesieve_sieve_get_setting
};

static void client_idle_timeout(struct client *client)
{
	if (client->cmd.func != NULL) {
		client_destroy(client,
			"Disconnected for inactivity in reading our output");
	} else {
		client_send_bye(client, "Disconnected for inactivity");
		client_destroy(client, "Disconnected for inactivity");
	}
}

static struct sieve_storage *
client_get_storage(struct sieve_instance *svinst, struct mail_user *user,
		   int fd_out)
{
	struct sieve_storage *storage;
	enum sieve_error error;
	const char *errormsg, *byemsg;

	/* Open personal script storage */

	storage = sieve_storage_create_main(svinst, user,
					    SIEVE_STORAGE_FLAG_READWRITE,
					    &error);
	if (storage == NULL) {
		switch (error) {
		case SIEVE_ERROR_NOT_POSSIBLE:
			byemsg = "BYE \"Sieve processing is disabled for this user.\"\r\n";
			errormsg = "Failed to open Sieve storage: "
				"Sieve is disabled for this user";
			break;
		case SIEVE_ERROR_NOT_FOUND:
			byemsg = "BYE \"This user cannot manage personal Sieve scripts.\"\r\n";
			errormsg = "Failed to open Sieve storage: "
				"Personal script storage disabled or not found.";
			break;
		default:
			byemsg = t_strflocaltime(
				"BYE \""CRITICAL_MSG_STAMP"\"\r\n", ioloop_time);
			errormsg = "Failed to open Sieve storage.";
		}

		if (write(fd_out, byemsg, strlen(byemsg)) < 0) {
			if (errno != EAGAIN && errno != EPIPE)
				i_error("write(client) failed: %m");
		}
		i_fatal("%s", errormsg);
	}

	return storage;
}

struct client *
client_create(int fd_in, int fd_out, const char *session_id,
	      struct event *event, struct mail_user *user,
	      struct mail_storage_service_user *service_user,
	      const struct managesieve_settings *set)
{
	struct client *client;
	const char *ident;
	struct sieve_environment svenv;
	struct sieve_instance *svinst;
	struct sieve_storage *storage;
	pool_t pool;

	/* Initialize Sieve */

	memset((void*)&svenv, 0, sizeof(svenv));
	svenv.username = user->username;
	(void)mail_user_get_home(user, &svenv.home_dir);
	svenv.base_dir = user->set->base_dir;
	svenv.event_parent = event;
	svenv.flags = SIEVE_FLAG_HOME_RELATIVE;

	svinst = sieve_init(&svenv, &managesieve_sieve_callbacks,
			    (void *) user, set->mail_debug);

	/* Get Sieve storage */

	storage = client_get_storage(svinst, user, fd_out);

	/* always use nonblocking I/O */
	net_set_nonblock(fd_in, TRUE);
	net_set_nonblock(fd_out, TRUE);

	pool = pool_alloconly_create("managesieve client", 1024);
	client = p_new(pool, struct client, 1);
	client->pool = pool;
	client->event = event;
	event_ref(client->event);
	client->set = set;
	client->service_user = service_user;
	client->session_id = p_strdup(pool, session_id);
	client->fd_in = fd_in;
	client->fd_out = fd_out;
	client->input = i_stream_create_fd(
		fd_in, set->managesieve_max_line_length);
	client->output = o_stream_create_fd(fd_out, (size_t)-1);

	o_stream_set_no_error_handling(client->output, TRUE);
	i_stream_set_name(client->input, "<managesieve client>");
	o_stream_set_name(client->output, "<managesieve client>");

	o_stream_set_flush_callback(client->output, client_output, client);

	client->io = io_add_istream(client->input, client_input, client);
	client->last_input = ioloop_time;
	client->to_idle = timeout_add(CLIENT_IDLE_TIMEOUT_MSECS,
				      client_idle_timeout, client);

	client->cmd.pool = pool_alloconly_create(
		MEMPOOL_GROWING"client command", 1024*12);
	client->cmd.client = client;
	client->cmd.event = event_create(client->event);
	client->user = user;

	if (set->rawlog_dir[0] != '\0') {
		(void)iostream_rawlog_create(set->rawlog_dir, &client->input,
					     &client->output);
	}

	client->parser = managesieve_parser_create(
		client->input, set->managesieve_max_line_length);

	client->svinst = svinst;
	client->storage = storage;

	ident = mail_user_get_anvil_userip_ident(client->user);
	if (ident != NULL) {
		master_service_anvil_send(
			master_service, t_strconcat("CONNECT\t", my_pid,
						    "\tsieve/", ident,
						    "\n", NULL));
		client->anvil_sent = TRUE;
	}

	managesieve_client_count++;
	DLLIST_PREPEND(&managesieve_clients, client);
	if (hook_client_created != NULL)
		hook_client_created(&client);

	managesieve_refresh_proctitle();
	return client;
}

static const char *client_stats(struct client *client)
{
	const struct var_expand_table logout_tab[] = {
		{ 'i', dec2str(i_stream_get_absolute_offset(client->input)),
		  "input" },
		{ 'o', dec2str(client->output->offset), "output" },
		{ '\0', dec2str(client->put_bytes), "put_bytes" },
		{ '\0', dec2str(client->put_count), "put_count" },
		{ '\0', dec2str(client->get_bytes), "get_bytes" },
		{ '\0', dec2str(client->get_count), "get_count" },
		{ '\0', dec2str(client->check_bytes), "check_bytes" },
		{ '\0', dec2str(client->check_count), "check_count" },
		{ '\0', dec2str(client->deleted_count), "deleted_count" },
		{ '\0', dec2str(client->renamed_count), "renamed_count" },
		{ '\0', client->session_id, "session" },
		{ '\0', NULL, NULL }
	};
	const struct var_expand_table *user_tab =
		mail_user_var_expand_table(client->user);
	const struct var_expand_table *tab =
		t_var_expand_merge_tables(logout_tab, user_tab);
	string_t *str;
	const char *error;

	str = t_str_new(128);
	if (var_expand_with_funcs(str, client->set->managesieve_logout_format,
				  tab, mail_user_var_expand_func_table,
				  client->user, &error) < 0) {
		i_error("Failed to expand managesieve_logout_format=%s: %s",
			client->set->managesieve_logout_format, error);
	}
	return str_c(str);
}

void client_destroy(struct client *client, const char *reason)
{
	bool ret;

 	i_assert(!client->handling_input);
	i_assert(!client->destroyed);
	client->destroyed = TRUE;

	if (!client->disconnected) {
		client->disconnected = TRUE;
		if (reason == NULL) {
			reason = io_stream_get_disconnect_reason(client->input,
								 client->output);
		}
		i_info("%s %s", reason, client_stats(client));
	}

	if (client->command_pending) {
		/* try to deinitialize the command */
		i_assert(client->cmd.func != NULL);

		i_stream_close(client->input);
		o_stream_close(client->output);

		client->input_pending = FALSE;

		ret = client->cmd.func(&client->cmd);
		i_assert(ret);
	}

	if (client->anvil_sent) {
		master_service_anvil_send(
			master_service, t_strconcat(
				"DISCONNECT\t", my_pid, "\tsieve/",
				mail_user_get_anvil_userip_ident(client->user),
				"\n", NULL));
	}

	managesieve_parser_destroy(&client->parser);
	io_remove(&client->io);
	timeout_remove(&client->to_idle_output);
	timeout_remove(&client->to_idle);

	/* i/ostreams are already closed at this stage, so fd can be closed */
	net_disconnect(client->fd_in);
	if (client->fd_in != client->fd_out)
		net_disconnect(client->fd_out);

	/* Free the user after client is already disconnected. It may start
	   some background work like autoexpunging. */
	mail_user_unref(&client->user);

	/* free the i/ostreams after mail_user_unref(), which could trigger
	   mail_storage_callbacks notifications that write to the ostream. */
	i_stream_destroy(&client->input);
	o_stream_destroy(&client->output);

	sieve_storage_unref(&client->storage);
	sieve_deinit(&client->svinst);

	event_unref(&client->cmd.event);
	pool_unref(&client->cmd.pool);
	mail_storage_service_user_unref(&client->service_user);

	managesieve_client_count--;
	DLLIST_REMOVE(&managesieve_clients, client);
	event_unref(&client->event);
	pool_unref(&client->pool);

	master_service_client_connection_destroyed(master_service);
	managesieve_refresh_proctitle();
}

static void client_destroy_timeout(struct client *client)
{
	client_destroy(client, NULL);
}

void client_disconnect(struct client *client, const char *reason)
{
	i_assert(reason != NULL);

	if (client->disconnected)
		return;

	i_info("Disconnected: %s %s", reason, client_stats(client));
	client->disconnected = TRUE;
	o_stream_flush(client->output);
	o_stream_uncork(client->output);

	i_stream_close(client->input);
	o_stream_close(client->output);

	timeout_remove(&client->to_idle);
	client->to_idle = timeout_add(0, client_destroy_timeout, client);
}

void client_disconnect_with_error(struct client *client, const char *msg)
{
	client_send_bye(client, msg);
	client_disconnect(client, msg);
}

int client_send_line(struct client *client, const char *data)
{
	struct const_iovec iov[2];

	if (client->output->closed)
		return -1;

	iov[0].iov_base = data;
	iov[0].iov_len = strlen(data);
	iov[1].iov_base = "\r\n";
	iov[1].iov_len = 2;

	if (o_stream_sendv(client->output, iov, 2) < 0)
		return -1;
	client->last_output = ioloop_time;

	if (o_stream_get_buffer_used_size(client->output) >=
	    CLIENT_OUTPUT_OPTIMAL_SIZE) {
		/* buffer full, try flushing */
		return o_stream_flush(client->output);
	}
	return 1;
}

void client_send_response(struct client *client, const char *oknobye,
			  const char *resp_code, const char *msg)
{
	string_t *str;

	str = t_str_new(128);
	str_append(str, oknobye);

	if (resp_code != NULL) {
		str_append(str, " (");
		str_append(str, resp_code);
		str_append_c(str, ')');
	}

	if (msg != NULL) {
		str_append_c(str, ' ');
		managesieve_quote_append_string(str, msg, TRUE);
	}

	client_send_line(client, str_c(str));
}

struct event_passthrough *
client_command_create_finish_event(struct client_command_context *cmd)
{
	struct event_passthrough *e =
		event_create_passthrough(cmd->event)->
		set_name("managesieve_command_finished");
	return e;
}

void client_send_command_error(struct client_command_context *cmd,
			       const char *msg)
{
	struct client *client = cmd->client;
	const char *error, *cmd_name;
	bool fatal;

	if (msg == NULL) {
		msg = managesieve_parser_get_error(client->parser, &fatal);
		if (fatal) {
			client_disconnect_with_error(client, msg);
			return;
		}
	}

	if (cmd->name == NULL) {
		error = t_strconcat("Error in MANAGESIEVE command: ",
				    msg, NULL);
	} else {
		cmd_name = t_str_ucase(cmd->name);
		error = t_strconcat("Error in MANAGESIEVE command ",
				    cmd_name, ": ", msg, NULL);
	}

	client_send_no(client, error);

	if (++client->bad_counter >= CLIENT_MAX_BAD_COMMANDS) {
		client_disconnect_with_error(
			client, "Too many invalid MANAGESIEVE commands.");
	}

	/* client_read_args() failures rely on this being set, so that the
	   command processing is stopped even while command function returns
	   FALSE. */
	cmd->param_error = TRUE;
}

#undef client_command_storage_error
void client_command_storage_error(struct client_command_context *cmd,
				  const char *source_filename,
				  unsigned int source_linenum,
				  const char *log_prefix, ...)
{
	struct event_log_params params = {
		.log_type = LOG_TYPE_INFO,
		.source_filename = source_filename,
		.source_linenum = source_linenum,
	};
	struct client *client = cmd->client;
	struct sieve_storage *storage = client->storage;
	enum sieve_error error_code;
	const char *error;
	va_list args;

	error = sieve_storage_get_last_error(storage, &error_code);

	switch (error_code) {
	case SIEVE_ERROR_TEMP_FAILURE:
		client_send_noresp(client, "TRYLATER", error);
		break;
	case SIEVE_ERROR_NO_QUOTA:
		client_send_noresp(client, "QUOTA", error);
		break;
	case SIEVE_ERROR_NOT_FOUND:
		client_send_noresp(client, "NONEXISTENT", error);
		break;
	case SIEVE_ERROR_ACTIVE:
		client_send_noresp(client, "ACTIVE", error);
		break;
	case SIEVE_ERROR_EXISTS:
		client_send_noresp(client, "ALREADYEXISTS", error);
		break;
	case SIEVE_ERROR_NOT_POSSIBLE:
	default:
		client_send_no(client, error);
		break;
	}

	struct event_passthrough *e =
		client_command_create_finish_event(cmd)->
		add_str("error", error);

	va_start(args, log_prefix);
	event_log(e->event(), &params, "%s: %s",
		  t_strdup_vprintf(log_prefix, args), error);
	va_end(args);
}

bool client_read_args(struct client_command_context *cmd, unsigned int count,
		      unsigned int flags, bool no_more,
		      const struct managesieve_arg **args_r)
{
	const struct managesieve_arg *dummy_args_r = NULL;
	string_t *str;
	int ret;

	if (args_r == NULL)
		args_r = &dummy_args_r;

	i_assert(count <= INT_MAX);

	ret = managesieve_parser_read_args(cmd->client->parser,
					   (no_more ? 0 : count),
					   flags, args_r);
	if (ret >= 0) {
		if (count > 0 || no_more) {
			if (ret < (int)count) {
				client_send_command_error(
					cmd, "Missing arguments.");
				return FALSE;
			} else if (no_more && ret > (int)count) {
				client_send_command_error(
					cmd, "Too many arguments.");
				return FALSE;
			}
		}

		str = t_str_new(256);
		managesieve_write_args(str, *args_r);
		cmd->args = p_strdup(cmd->pool, str_c(str));

		event_add_str(cmd->event, "cmd_args", cmd->args);

		/* all parameters read successfully */
		return TRUE;
	} else if (ret == -2) {
		/* need more data */
		if (cmd->client->input->closed) {
			/* disconnected */
 			cmd->param_error = TRUE;
		}
		return FALSE;
	} else {
		/* error */
		client_send_command_error(cmd, NULL);
		return FALSE;
	}
}

bool client_read_string_args(struct client_command_context *cmd,
			     bool no_more, unsigned int count, ...)
{
	const struct managesieve_arg *msieve_args;
	va_list va;
	const char *str;
	unsigned int i;
	bool result = TRUE;

	if (!client_read_args(cmd, count, 0, no_more, &msieve_args))
		return FALSE;

	va_start(va, count);
	for (i = 0; i < count; i++) {
		const char **ret = va_arg(va, const char **);

		if (MANAGESIEVE_ARG_IS_EOL(&msieve_args[i])) {
			client_send_command_error(cmd, "Missing arguments.");
			result = FALSE;
			break;
		}

		if (!managesieve_arg_get_string(&msieve_args[i], &str)) {
			client_send_command_error(cmd, "Invalid arguments.");
			result = FALSE;
			break;
		}

		if (ret != NULL)
			*ret = str;
	}
	va_end(va);

	return result;
}

void _client_reset_command(struct client *client)
{
	pool_t pool;
	size_t size;

	/* reset input idle time because command output might have taken a long
	   time and we don't want to disconnect client immediately then */
	client->last_input = ioloop_time;
	timeout_reset(client->to_idle);

	client->command_pending = FALSE;
	if (client->io == NULL && !client->disconnected) {
		i_assert(i_stream_get_fd(client->input) >= 0);
		client->io = io_add(i_stream_get_fd(client->input),
				    IO_READ, client_input, client);
	}
	o_stream_set_flush_callback(client->output, client_output, client);

	event_unref(&client->cmd.event);

	pool = client->cmd.pool;
	i_zero(&client->cmd);

	p_clear(pool);
	client->cmd.pool = pool;
	client->cmd.client = client;

	client->cmd.event = event_create(client->event);

	managesieve_parser_reset(client->parser);

	/* if there's unread data in buffer, remember that there's input pending
	   and we should get around to calling client_input() soon. This is
	   mostly for APPEND/IDLE. */
	(void)i_stream_get_data(client->input, &size);
	if (size > 0 && !client->destroyed)
		client->input_pending = TRUE;
}

/* Skip incoming data until newline is found,
   returns TRUE if newline was found. */
static bool client_skip_line(struct client *client)
{
	const unsigned char *data;
	size_t i, data_size;

	data = i_stream_get_data(client->input, &data_size);

	for (i = 0; i < data_size; i++) {
		if (data[i] == '\n') {
			client->input_skip_line = FALSE;
			i++;
			break;
		}
	}

	i_stream_skip(client->input, i);
	return !client->input_skip_line;
}

static bool client_handle_input(struct client_command_context *cmd)
{
	struct client *client = cmd->client;

	if (cmd->func != NULL) {
		bool finished;

		event_push_global(cmd->event);
		finished = cmd->func(cmd);
		event_pop_global(cmd->event);

		/* command is being executed - continue it */
		if (finished || cmd->param_error) {
			/* command execution was finished */
			if (!cmd->param_error)
				client->bad_counter = 0;
			_client_reset_command(client);
			return TRUE;
		}

		/* unfinished */
		if (client->command_pending)
			o_stream_set_flush_pending(client->output, TRUE);
		return FALSE;
	}

	if (client->input_skip_line) {
		/* we're just waiting for new line.. */
		if (!client_skip_line(client))
			return FALSE;

		/* got the newline */
		_client_reset_command(client);

		/* pass through to parse next command */
	}

	if (cmd->name == NULL) {
		cmd->name = managesieve_parser_read_word(client->parser);
		if (cmd->name == NULL)
			return FALSE; /* need more data */
		cmd->name = p_strdup(cmd->pool, cmd->name);
		managesieve_refresh_proctitle();
	}

	if (cmd->name[0] == '\0') {
		/* command not given - cmd_func is already NULL. */
	} else {
		/* find the command function */
		struct command *command = command_find(cmd->name);

		if (command != NULL)
			cmd->func = command->func;
	}

	client->input_skip_line = TRUE;
	if (cmd->func == NULL) {
		/* unknown command */
		client_send_command_error(cmd, "Unknown command.");
		_client_reset_command(client);
	} else {
		i_assert(!client->disconnected);

		event_add_str(cmd->event, "cmd_name", t_str_ucase(cmd->name));
		client_handle_input(cmd);
	}

	return TRUE;
}

void client_input(struct client *client)
{
	struct client_command_context *cmd = &client->cmd;
	bool ret;

	if (client->command_pending) {
		/* already processing one command. wait. */
		io_remove(&client->io);
		return;
	}

	client->input_pending = FALSE;
	client->last_input = ioloop_time;
	timeout_reset(client->to_idle);

	switch (i_stream_read(client->input)) {
	case -1:
		/* disconnected */
		client_destroy(client, NULL);
		return;
	case -2:
		/* parameter word is longer than max. input buffer size.
		   this is most likely an error, so skip the new data
		   until newline is found. */
		client->input_skip_line = TRUE;

		client_send_command_error(cmd, "Too long argument.");
		_client_reset_command(client);
		break;
	}

	client->handling_input = TRUE;
	o_stream_cork(client->output);
	do {
		T_BEGIN {
			ret = client_handle_input(cmd);
		} T_END;
	} while (ret && !client->disconnected);
	o_stream_uncork(client->output);
	client->handling_input = FALSE;

	if (client->command_pending)
		client->input_pending = TRUE;

	if (client->output->closed)
		client_destroy(client, NULL);
}

int client_output(struct client *client)
{
	struct client_command_context *cmd = &client->cmd;
	int ret;
	bool finished;

	client->last_output = ioloop_time;
	timeout_reset(client->to_idle);
	if (client->to_idle_output != NULL)
		timeout_reset(client->to_idle_output);

	if ((ret = o_stream_flush(client->output)) < 0) {
		client_destroy(client, NULL);
		return 1;
	}

	if (!client->command_pending)
		return 1;

	/* continue processing command */
	o_stream_cork(client->output);
	client->output_pending = TRUE;
	finished = cmd->func(cmd) || cmd->param_error;

	/* a bit kludgy check. normally we would want to get back to this
	   output handler, but IDLE is a special case which has command
	   pending but without necessarily anything to write. */
	if (!finished && client->output_pending)
		o_stream_set_flush_pending(client->output, TRUE);

	o_stream_uncork(client->output);

	if (finished) {
		/* command execution was finished */
		client->bad_counter = 0;
		_client_reset_command(client);

		if (client->input_pending)
			client_input(client);
	}
	return ret;
}

void clients_destroy_all(void)
{
	while (managesieve_clients != NULL) {
		client_send_bye(managesieve_clients, "Server shutting down.");
		client_destroy(managesieve_clients, "Server shutting down.");
	}
}
