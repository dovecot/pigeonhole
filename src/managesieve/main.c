/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "buffer.h"
#include "ioloop.h"
#include "istream.h"
#include "istream-concat.h"
#include "ostream.h"
#include "path-util.h"
#include "str.h"
#include "base64.h"
#include "process-title.h"
#include "restrict-access.h"
#include "time-util.h"
#include "settings.h"
#include "master-interface.h"
#include "master-service-settings.h"
#include "master-admin-client.h"
#include "master-service.h"
#include "login-server.h"
#include "mail-user.h"
#include "mail-storage-service.h"

#include "managesieve-quote.h"
#include "managesieve-common.h"
#include "managesieve-commands.h"
#include "managesieve-capabilities.h"

#include <stdio.h>
#include <unistd.h>

#define IS_STANDALONE() \
        (getenv(MASTER_IS_PARENT_ENV) == NULL)

#define MANAGESIEVE_DIE_IDLE_SECS 10

static bool verbose_proctitle = FALSE;
static struct mail_storage_service_ctx *storage_service;
static struct login_server *login_server = NULL;

void (*hook_client_created)(struct client **client) = NULL;

struct event_category event_category_managesieve = {
	.name = "managesieve",
};

void managesieve_refresh_proctitle(void)
{
#define MANAGESIEVE_PROCTITLE_PREFERRED_LEN 80
	struct client *client;
	string_t *title = t_str_new(128);

	if (!verbose_proctitle)
		return;

	str_append_c(title, '[');
	switch (managesieve_client_count) {
	case 0:
		str_append(title, "idling");
		break;
	case 1:
		client = managesieve_clients;
		str_append(title, client->user->username);
		if (client->user->conn.remote_ip != NULL) {
			str_append_c(title, ' ');
			str_append(title,
				   net_ip2addr(client->user->conn.remote_ip));
		}

		if (client->cmd.name != NULL &&
		    str_len(title) <= MANAGESIEVE_PROCTITLE_PREFERRED_LEN) {
			str_append_c(title, ' ');
			str_append(title, client->cmd.name);
		}
		break;
	default:
		str_printfa(title, "%u connections", managesieve_client_count);
		break;
	}
	str_append_c(title, ']');
	process_title_set(str_c(title));
}

static void client_kill_idle(struct client *client)
{
	mail_storage_service_io_activate_user(client->user->service_user);
	client_send_bye(client, MASTER_SERVICE_SHUTTING_DOWN_MSG".");
	client_destroy(client, MASTER_SERVICE_SHUTTING_DOWN_MSG);
}

static void managesieve_die(void)
{
	struct client *client, *next;
	time_t last_io, now = time(NULL);
	time_t stop_timestamp = now - MANAGESIEVE_DIE_IDLE_SECS;
	unsigned int stop_msecs;

	for (client = managesieve_clients; client != NULL; client = next) {
		next = client->next;

		last_io = I_MAX(client->last_input, client->last_output);
		if (last_io <= stop_timestamp)
			client_kill_idle(client);
		else {
			timeout_remove(&client->to_idle);
			stop_msecs = (last_io - stop_timestamp) * 1000;
			client->to_idle = timeout_add(stop_msecs,
						      client_kill_idle, client);
		}
	}
}

static void client_add_istream_prefix(struct client *client,
				      const buffer_t *input)
{
	struct istream *inputs[] = {
		i_stream_create_copy_from_data(input->data, input->used),
		client->input,
		NULL
	};
	client->input = i_stream_create_concat(inputs);
	i_stream_copy_fd(client->input, inputs[1]);
	i_stream_unref(&inputs[0]);
	i_stream_unref(&inputs[1]);

	i_stream_set_input_pending(client->input, TRUE);
}

static void client_logged_in(struct client *client)
{
	struct ostream *output;

	output = client->output;
	o_stream_ref(output);
	o_stream_cork(output);
	if (!IS_STANDALONE())
		client_send_ok(client, "Logged in.");
	(void)client_input(client);
	o_stream_uncork(output);
	o_stream_unref(&output);
}

static int
client_create_from_input(const struct mail_storage_service_input *input,
			 int fd_in, int fd_out, const buffer_t *input_buf,
			 const char **client_error_r, const char **error_r)
{
	struct mail_storage_service_input service_input;
	struct mail_user *mail_user;
	struct client *client;
	struct managesieve_settings *set;
	struct event *event;

	*client_error_r = NULL;
	*error_r = NULL;

	event = event_create(NULL);
	event_add_category(event, &event_category_managesieve);
	event_add_fields(event, (const struct event_add_field []){
		{ .key = "user", .value = input->username },
		{ .key = "session", .value = input->session_id },
		{ .key = NULL }
	});

	service_input = *input;
	service_input.event_parent = event;
	if (mail_storage_service_lookup_next(storage_service, &service_input,
					     &mail_user, error_r) <= 0) {
		event_unref(&event);
		return -1;
	}
	restrict_access_allow_coredumps(TRUE);

	if (settings_get(mail_user->event, &managesieve_setting_parser_info, 0,
			 &set, error_r) < 0) {
		mail_user_unref(&mail_user);
		event_unref(&event);
		return -1;
	}
	if (set->verbose_proctitle)
		verbose_proctitle = TRUE;

	if (client_create(fd_in, fd_out, input->session_id,
			  event, mail_user, set,
			  &client, client_error_r, error_r) < 0) {
		settings_free(set);
		mail_user_unref(&mail_user);
		event_unref(&event);
		return -1;
	}
	if (input_buf != NULL && input_buf->used > 0)
		client_add_istream_prefix(client, input_buf);
	client_create_finish(client);
	T_BEGIN {
		client_logged_in(client);
	} T_END;
	event_unref(&event);
	return 0;
}

static void main_stdio_run(const char *username)
{
	struct mail_storage_service_input input;
	const char *value, *client_error, *error, *input_base64;
	buffer_t *input_buf;

	i_zero(&input);
	input.service = "sieve";
	input.username =  username != NULL ? username : getenv("USER");
	if (input.username == NULL && IS_STANDALONE())
		input.username = getlogin();
	if (input.username == NULL)
		i_fatal("USER environment missing");
	if ((value = getenv("IP")) != NULL)
		net_addr2ip(value, &input.remote_ip);
	if ((value = getenv("LOCAL_IP")) != NULL)
		net_addr2ip(value, &input.local_ip);

	input_base64 = getenv("CLIENT_INPUT");
	input_buf = (input_base64 == NULL ?
		     NULL : t_base64_decode_str(input_base64));

	if (client_create_from_input(&input, STDIN_FILENO, STDOUT_FILENO,
				     input_buf, &client_error, &error) < 0)
		i_fatal("%s", error);
}

static void
login_request_finished(const struct login_server_request *request,
		       const char *username, const char *const *extra_fields)
{
	struct mail_storage_service_input input;
	enum login_request_flags flags = request->auth_req.flags;
	const char *client_error, *error;
	buffer_t input_buf;

	i_zero(&input);
	input.service = "sieve";
	input.local_ip = request->auth_req.local_ip;
	input.remote_ip = request->auth_req.remote_ip;
	input.local_port = request->auth_req.local_port;
	input.remote_port = request->auth_req.remote_port;
	input.username = username;
	input.userdb_fields = extra_fields;
	input.session_id = request->session_id;
	if ((flags & LOGIN_REQUEST_FLAG_END_CLIENT_SECURED_TLS) != 0)
		input.end_client_tls_secured = TRUE;

	buffer_create_from_const_data(&input_buf, request->data,
				      request->auth_req.data_size);
	if (client_create_from_input(&input, request->fd, request->fd,
				     &input_buf, &client_error, &error) < 0) {
		string_t *byemsg;
		int fd = request->fd;

		if (client_error == NULL) {
			client_error = t_strflocaltime(CRITICAL_MSG_STAMP,
						       ioloop_time);
		}

		byemsg = t_str_new(256);
		str_append(byemsg, "BYE ");
		managesieve_quote_append_string(byemsg, client_error, FALSE);
		str_append(byemsg, "\r\n");

		if (write(fd, str_data(byemsg), str_len(byemsg)) < 0) {
			if (errno != EAGAIN && errno != EPIPE)
				i_error("write(client) failed: %m");
		}
		i_error("%s", error);
		i_close_fd(&fd);
		master_service_client_connection_destroyed(master_service);
	}
}

static void
login_request_failed(const struct login_server_request *request,
		     const char *errormsg)
{
	const char *msg;

	msg = t_strdup_printf("NO \"%s\"\r\n", errormsg);
	if (write(request->fd, msg, strlen(msg)) < 0) {
		/* ignored */
	}
}

static unsigned int
master_admin_cmd_kick_user(const char *user, const guid_128_t conn_guid)
{
	struct client *client, *next;
	unsigned int count = 0;

	for (client = managesieve_clients; client != NULL; client = next) {
		next = client->next;
		if (strcmp(client->user->username, user) == 0 &&
		    (guid_128_is_empty(conn_guid) ||
		     guid_128_cmp(client->anvil_conn_guid, conn_guid) == 0))
			client_kick(client);
	}
	return count;
}

static const struct master_admin_client_callback admin_callbacks = {
	.cmd_kick_user = master_admin_cmd_kick_user,
};

static void client_connected(struct master_service_connection *conn)
{
	/* When running standalone, we shouldn't even get here */
	i_assert(login_server != NULL);

	master_service_client_connection_accept(conn);
	login_server_add(login_server, conn->fd);
}

int main(int argc, char *argv[])
{
	struct login_server_settings login_set;
	enum master_service_flags service_flags = 0;
	enum mail_storage_service_flags storage_service_flags = 0;
	const char *username = NULL, *error = NULL;
	int c;

	i_zero(&login_set);
	login_set.postlogin_timeout_secs =
		LOGIN_SERVER_POSTLOGIN_TIMEOUT_DEFAULT;

	if (IS_STANDALONE() && getuid() == 0 &&
	    net_getpeername(1, NULL, NULL) == 0) {
		printf("NO \"managesieve binary must not be started from "
		       "inetd, use managesieve-login instead.\"\n");
		return 1;
	}

	if (IS_STANDALONE() || getenv("DUMP_CAPABILITY") != NULL) {
		service_flags |= MASTER_SERVICE_FLAG_STANDALONE |
				 MASTER_SERVICE_FLAG_STD_CLIENT;
	}
	if (getenv("DUMP_CAPABILITY") != NULL)
		service_flags |= MASTER_SERVICE_FLAG_DONT_SEND_STATS;

	master_service = master_service_init("managesieve", service_flags,
					     &argc, &argv, "t:u:");
	while ((c = master_getopt(master_service)) > 0) {
		switch (c) {
		case 't':
			if (str_to_uint(optarg,
					&login_set.postlogin_timeout_secs) < 0 ||
			    login_set.postlogin_timeout_secs == 0)
				i_fatal("Invalid -t parameter: %s", optarg);
			break;
		case 'u':
			storage_service_flags |=
				MAIL_STORAGE_SERVICE_FLAG_USERDB_LOOKUP;
			username = optarg;
			break;
		default:
			return FATAL_DEFAULT;
		}
	}

	master_admin_clients_init(&admin_callbacks);
	master_service_set_die_callback(master_service, managesieve_die);

	/* Plugins may want to add commands, so this needs to be called early.
	 */
	commands_init();

	/* Dump capabilities if requested */
	if (getenv("DUMP_CAPABILITY") != NULL) {
		struct master_service_settings_input set_input = {
			.hide_obsolete_warnings = TRUE,
		};
		struct master_service_settings_output set_output;
		if (master_service_settings_read(master_service, &set_input,
						 &set_output, &error) < 0)
			i_fatal("%s", error);

		i_set_debug_file("/dev/null");
		managesieve_capabilities_dump();
		commands_deinit();
		master_service_deinit(&master_service);
		exit(0);
	}

	if (master_service_settings_read_simple(master_service, &error) < 0)
		i_fatal("%s", error);

	if (t_abspath("auth-master", &login_set.auth_socket_path, &error) < 0)
		i_fatal("t_abspath(%s) failed: %s", "auth-master", error);

	if (argv[optind] != NULL &&
	    t_abspath(argv[optind], &login_set.postlogin_socket_path,
		      &error) < 0) {
		i_fatal("t_abspath(%s) failed: %s", argv[optind], error);
	}

	login_set.callback = login_request_finished;
	login_set.failure_callback = login_request_failed;

	if (!IS_STANDALONE())
		login_server = login_server_init(master_service, &login_set);

	storage_service =
		mail_storage_service_init(master_service,
					  storage_service_flags);
	master_service_init_finish(master_service);
	/* NOTE: login_set.*_socket_path are now invalid due to data stack
	   having been freed */

	/* Fake that we're running, so we know if client was destroyed while
	   handling its initial input.
	 */
	io_loop_set_running(current_ioloop);

	if (IS_STANDALONE()) {
		T_BEGIN {
			main_stdio_run(username);
		} T_END;
	} else {
		io_loop_set_running(current_ioloop);
	}

	if (io_loop_is_running(current_ioloop))
		master_service_run(master_service, client_connected);
	clients_destroy_all();

	if (login_server != NULL)
		login_server_deinit(&login_server);
	mail_storage_service_deinit(&storage_service);

	commands_deinit();

	master_service_deinit(&master_service);
	return 0;
}
