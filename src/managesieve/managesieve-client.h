#ifndef MANAGESIEVE_CLIENT_H
#define MANAGESIEVE_CLIENT_H

#include "guid.h"
#include "managesieve-commands.h"

struct client;
struct sieve_storage;
struct managesieve_parser;
struct managesieve_arg;

struct client_command_context {
	struct client *client;
	struct event *event;

	pool_t pool;
	/* Name of this command */
	const char *name;
	/* Parameters for this command. These are generated from parsed
	   ManageSieve arguments, so they may not be exactly the same as how
	   client sent them. */
	const char *args;

	struct {
		uint64_t bytes_in;
		uint64_t bytes_out;
	} stats;
	command_func_t *func;
	void *context;

	bool param_error:1;
};

struct managesieve_module_register {
	unsigned int id;
};

union managesieve_module_context {
	struct managesieve_module_register *reg;
};
extern struct managesieve_module_register managesieve_module_register;

struct client {
	struct client *prev, *next;

	struct event *event;
	const char *session_id;
	int fd_in, fd_out;
	struct io *io;
	struct istream *input;
	struct ostream *output;
	struct timeout *to_idle, *to_idle_output;
	guid_128_t anvil_conn_guid;

	pool_t pool;
	const struct managesieve_settings *set;

	struct mail_user *user;

	struct sieve_instance *svinst;
	struct sieve_storage *storage;

	time_t last_input, last_output;
	unsigned int bad_counter;

	struct managesieve_parser *parser;
	struct client_command_context cmd;

	uoff_t put_bytes;
	uoff_t get_bytes;
	uoff_t check_bytes;
	unsigned int put_count;
	unsigned int get_count;
	unsigned int check_count;
	unsigned int deleted_count;
	unsigned int renamed_count;

	bool disconnected:1;
	bool destroyed:1;
	bool command_pending:1;
	bool input_pending:1;
	bool output_pending:1;
	bool handling_input:1;
	bool anvil_sent:1;
	bool input_skip_line:1; /* skip all the data until we've found a new
	                           line */
};

extern struct client *managesieve_clients;
extern unsigned int managesieve_client_count;

/* Create new client with specified input/output handles. socket specifies
   if the handle is a socket. */
int client_create(int fd_in, int fd_out, const char *session_id,
		  struct event *event, struct mail_user *user,
		  const struct managesieve_settings *set,
		  struct client **client_r, const char **client_error_r,
		  const char **error_r);
void client_create_finish(struct client *client);
void client_destroy(struct client *client, const char *reason);

void client_dump_capability(struct client *client);

/* Disconnect client connection */
void client_disconnect(struct client *client, const char *reason);
void client_disconnect_with_error(struct client *client, const char *msg);

/* Send a line of data to client. Returns 1 if ok, 0 if buffer is getting full,
   -1 if error */
int client_send_line(struct client *client, const char *data);

void client_send_response(struct client *client, const char *oknobye,
			  const char *resp_code, const char *msg);

#define client_send_ok(client, msg) \
	client_send_response(client, "OK", NULL, msg)
#define client_send_no(client, msg) \
	client_send_response(client, "NO", NULL, msg)
#define client_send_bye(client, msg) \
	client_send_response(client, "BYE", NULL, msg)

#define client_send_okresp(client, resp_code, msg) \
	client_send_response(client, "OK", resp_code, msg)
#define client_send_noresp(client, resp_code, msg) \
	client_send_response(client, "NO", resp_code, msg)
#define client_send_byeresp(cmd, resp_code, msg) \
	client_send_response(client, "BYE", resp_code, msg)

struct event_passthrough *
client_command_create_finish_event(struct client_command_context *cmd);

/* Send BAD command error to client. msg can be NULL. */
void client_send_command_error(struct client_command_context *cmd,
			       const char *msg);

/* Send storage or sieve-related errors to the client. Returns command finish
   event with the "error" field set accordingly. */
void client_command_storage_error(struct client_command_context *cmd,
				  const char *source_filename,
				  unsigned int source_linenum,
				  const char *log_prefix, ...)
				  ATTR_FORMAT(4, 5);
#define client_command_storage_error(cmd, ...) \
	client_command_storage_error(cmd, __FILE__, __LINE__, __VA_ARGS__)

/* Read a number of arguments. Returns TRUE if everything was read or
   FALSE if either needs more data or error occurred. */
bool client_read_args(struct client_command_context *cmd, unsigned int count,
		      unsigned int flags, bool no_more,
		      const struct managesieve_arg **args_r);
/* Reads a number of string arguments. ... is a list of pointers where to
   store the arguments. */
bool client_read_string_args(struct client_command_context *cmd, bool no_more,
			     unsigned int count, ...);

static inline bool client_read_no_args(struct client_command_context *cmd)
{
	return client_read_args(cmd, 0, 0, TRUE, NULL);
}

void _client_reset_command(struct client *client);
void client_input(struct client *client);
int client_output(struct client *client);

void client_kick(struct client *client);
void clients_destroy_all(void);

#endif
