/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "ioloop.h"
#include "array.h"
#include "istream-private.h"
#include "ostream.h"

#include "script-client-private.h"

#include <unistd.h>

#define MAX_OUTBUF_SIZE 16384

static void script_client_timeout(struct script_client *sclient)
{
	script_client_fail(sclient, SCRIPT_CLIENT_ERROR_RUN_TIMEOUT);
}

static void script_client_connect_timeout(struct script_client *sclient)
{
	script_client_fail(sclient, SCRIPT_CLIENT_ERROR_CONNECT_TIMEOUT);
}

static int script_client_connect(struct script_client *sclient)
{
	if (sclient->set->client_connect_timeout_msecs != 0) {
		sclient->to = timeout_add
			(sclient->set->client_connect_timeout_msecs,
				script_client_connect_timeout, sclient);
	}

	if ( sclient->connect(sclient) < 0 ) {
		script_client_fail(sclient, SCRIPT_CLIENT_ERROR_IO);
		return -1;
	}

	return 1;
}

static void script_client_disconnect
(struct script_client *sclient, bool force)
{
	int ret, error = FALSE;

	if ( sclient->ioloop != NULL )
		io_loop_stop(sclient->ioloop);

	if ( sclient->disconnected )
		return;

	if ( (ret=sclient->close_output(sclient)) < 0 )
		error = TRUE;
	
	if ( (ret=sclient->disconnect(sclient, force)) < 0 )
		error = TRUE;

	if ( sclient->script_input != NULL )
		i_stream_destroy(&sclient->script_input);
	if ( sclient->script_output != NULL )
		o_stream_destroy(&sclient->script_output);

	if ( sclient->to != NULL )
		timeout_remove(&sclient->to);
	if ( sclient->io != NULL )
		io_remove(&sclient->io);

	if (sclient->fd_in != -1 && close(sclient->fd_in) < 0)
		i_error("close(%s) failed: %m", sclient->path);
	if (sclient->fd_out != -1 && sclient->fd_out != sclient->fd_out)
		i_error("close(%s/out) failed: %m", sclient->path);
	sclient->fd_in = sclient->fd_out = -1;
	
	sclient->disconnected = TRUE;
	if (error && sclient->error == SCRIPT_CLIENT_ERROR_NONE ) {
		sclient->error = SCRIPT_CLIENT_ERROR_UNKNOWN;
	}
}

void script_client_fail
(struct script_client *sclient, enum script_client_error error)
{
	if ( sclient->error != SCRIPT_CLIENT_ERROR_NONE )
		return;

	sclient->error = error;
	script_client_disconnect(sclient, TRUE);

	sclient->failure(sclient, error);
}

static int script_client_script_output(struct script_client *sclient)
{
	struct istream *input = sclient->input;
	struct ostream *output = sclient->script_output;
	const unsigned char *data;
	size_t size;
	int ret = 0;

	if ((ret = o_stream_flush(output)) <= 0) {
		if (ret < 0)
			script_client_fail(sclient, SCRIPT_CLIENT_ERROR_IO);
		return ret;
	}

	if ( input != NULL && output != NULL ) {
		do {
			while ( (data=i_stream_get_data(input, &size)) != NULL ) {
				ssize_t sent;
	
				if ( (sent=o_stream_send(output, data, size)) < 0 ) {
					script_client_fail(sclient, SCRIPT_CLIENT_ERROR_IO);
					return -1;
				}
	
				if ( sent == 0 )
					return 0;
				i_stream_skip(input, sent);
			}
		} while ( (ret=i_stream_read(input)) > 0 );

		if ( ret == 0 ) {
			// FIXME: not supposed to happen; returning 0 will poll the input stream
			return 0;
		}

		if ( ret < 0 ) {
			if ( !input->eof ) {
				script_client_fail(sclient, SCRIPT_CLIENT_ERROR_IO);
				return -1;
			} else if ( !i_stream_have_bytes_left(input) ) {
				i_stream_unref(&sclient->input);
				input = NULL;

				if ( (ret = o_stream_flush(output)) <= 0 ) {
					if ( ret < 0 )
						script_client_fail(sclient, SCRIPT_CLIENT_ERROR_IO);
					return ret;
				}
			} 
		}
	}

	if ( input == NULL ) {
		o_stream_unref(&sclient->script_output);

		if ( sclient->script_input == NULL ) {
			script_client_disconnect(sclient, FALSE);
		} else {
			sclient->close_output(sclient);
		}
		return 0;
	}

	return 1;
}

static void script_client_script_input(struct script_client *sclient)
{
	struct istream *input = sclient->script_input;
	struct ostream *output = sclient->output;
	const unsigned char *data;
	size_t size;
	int ret = 0;

	if ( input != NULL ) {
		while ( (ret=i_stream_read_data(input, &data, &size, 0)) > 0 ) {
			if ( output != NULL ) {
				ssize_t sent;

				if ( (sent=o_stream_send(output, data, size)) < 0 ) {
					script_client_fail(sclient, SCRIPT_CLIENT_ERROR_IO);
					return;
				}
				size = (size_t)sent;
			}

			i_stream_skip(input, size);
		}

		if ( ret < 0 ) {
			if ( input->eof ) {
				script_client_disconnect(sclient, FALSE);
				return;
			}
			script_client_fail(sclient, SCRIPT_CLIENT_ERROR_IO);
		}
	}
}

int script_client_script_connected
(struct script_client *sclient)
{
	int ret = 1;

	sclient->start_time = ioloop_time;
	if (sclient->to != NULL)
		timeout_remove(&sclient->to);
	if ( sclient->set->input_idle_timeout_secs != 0 ) {
		sclient->to = timeout_add(sclient->set->input_idle_timeout_secs*1000,
      script_client_timeout, sclient);
	}

	/* run output */
	if ( sclient->script_output != NULL &&
		(ret=script_client_script_output(sclient)) == 0 ) {
		if ( sclient->script_output != NULL ) {
			o_stream_set_flush_callback
				(sclient->script_output, script_client_script_output, sclient);
		}
	}

	return ret;
}

void script_client_init
(struct script_client *sclient, pool_t pool, const char *path,
	const char *const *args, const struct script_client_settings *set)
{
	sclient->pool = pool;
	sclient->path = p_strdup(pool, path);
	if ( args != NULL )
		sclient->args = p_strarray_dup(pool, args);
	sclient->set = set;
	sclient->debug = set->debug;
	sclient->fd_in = -1;
	sclient->fd_out = -1;
}

void script_client_set_input
(struct script_client *sclient, struct istream *input)
{
	if ( sclient->input )
		i_stream_unref(&sclient->input);
	if ( input != NULL )
		i_stream_ref(input);
	sclient->input = input;
}

void script_client_set_output
(struct script_client *sclient, struct ostream *output)
{
	if ( sclient->output )
		o_stream_unref(&sclient->output);
	if ( output != NULL )
		o_stream_ref(output);
	sclient->output = output;
}

void script_client_set_env
(struct script_client *sclient, const char *name, const char *value)
{
	const char *env;

	if ( !array_is_created(&sclient->envs) )
		p_array_init(&sclient->envs, sclient->pool, 16);

	env = p_strdup_printf(sclient->pool, "%s=%s", name, value);
	array_append(&sclient->envs, &env, 1);
}

void script_client_init_streams(struct script_client *sclient)
{
	if ( sclient->fd_out >= 0 ) {
		sclient->script_output =
			o_stream_create_fd(sclient->fd_out, MAX_OUTBUF_SIZE, FALSE);
	}
	if ( sclient->fd_in >= 0 ) {
		sclient->script_input =
			i_stream_create_fd(sclient->fd_in, (size_t)-1, FALSE);
		sclient->io = io_add
			(sclient->fd_in, IO_READ, script_client_script_input, sclient);
	}
}

void script_client_destroy(struct script_client **_sclient)
{
	struct script_client *sclient = *_sclient;

	script_client_disconnect(sclient, TRUE);

	if ( sclient->input != NULL )
		i_stream_unref(&sclient->input);
	if ( sclient->output != NULL )
		o_stream_unref(&sclient->output);
	if ( sclient->io != NULL )
		io_remove(&sclient->io);
	if ( sclient->ioloop != NULL )
		io_loop_destroy(&sclient->ioloop);

	pool_unref(&sclient->pool);
	*_sclient = NULL;
}

int script_client_run(struct script_client *sclient)
{
	int ret;

	sclient->ioloop = io_loop_create();

	if ( script_client_connect(sclient) >= 0 ) {
		/* run output */
		ret = 1;		
		if ( sclient->script_output != NULL &&
			(ret=o_stream_flush(sclient->script_output)) == 0 ) {
			o_stream_set_flush_callback
				(sclient->script_output, script_client_script_output, sclient);
		}

		/* run i/o event loop */
		if ( ret < 0 ) {
			sclient->error = SCRIPT_CLIENT_ERROR_IO;
		} else if ( sclient->io != NULL || ret == 0 ) {
			io_loop_run(sclient->ioloop);
		}

		/* finished */
		script_client_disconnect(sclient, FALSE);
	}

	io_loop_destroy(&sclient->ioloop);

	if ( sclient->error != SCRIPT_CLIENT_ERROR_NONE )
		return -1;

	return sclient->exit_code;
}

