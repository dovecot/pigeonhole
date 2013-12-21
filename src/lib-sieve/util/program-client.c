/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "ioloop.h"
#include "array.h"
#include "istream-private.h"
#include "ostream.h"

#include "program-client-private.h"

#include <unistd.h>

#define MAX_OUTBUF_SIZE 16384

static void program_client_timeout(struct program_client *pclient)
{
	program_client_fail(pclient, PROGRAM_CLIENT_ERROR_RUN_TIMEOUT);
}

static void program_client_connect_timeout(struct program_client *pclient)
{
	program_client_fail(pclient, PROGRAM_CLIENT_ERROR_CONNECT_TIMEOUT);
}

static int program_client_connect(struct program_client *pclient)
{
	if (pclient->set->client_connect_timeout_msecs != 0) {
		pclient->to = timeout_add
			(pclient->set->client_connect_timeout_msecs,
				program_client_connect_timeout, pclient);
	}

	if ( pclient->connect(pclient) < 0 ) {
		program_client_fail(pclient, PROGRAM_CLIENT_ERROR_IO);
		return -1;
	}

	return 1;
}

static int program_client_close_output(struct program_client *pclient)
{
	int ret;

	if ( (ret=pclient->close_output(pclient)) < 0 )
		return -1;
	if ( pclient->program_output != NULL )
		o_stream_destroy(&pclient->program_output);
	pclient->program_output = NULL;

	return ret;
}

static void program_client_disconnect
(struct program_client *pclient, bool force)
{
	int ret, error = FALSE;

	if ( pclient->ioloop != NULL )
		io_loop_stop(pclient->ioloop);

	if ( pclient->disconnected )
		return;

	if ( (ret=program_client_close_output(pclient)) < 0 )
		error = TRUE;
	
	if ( (ret=pclient->disconnect(pclient, force)) < 0 )
		error = TRUE;

	if ( pclient->program_input != NULL )
		i_stream_destroy(&pclient->program_input);
	if ( pclient->program_output != NULL )
		o_stream_destroy(&pclient->program_output);

	if ( pclient->to != NULL )
		timeout_remove(&pclient->to);
	if ( pclient->io != NULL )
		io_remove(&pclient->io);

	if (pclient->fd_in != -1 && close(pclient->fd_in) < 0)
		i_error("close(%s) failed: %m", pclient->path);
	if (pclient->fd_out != -1 && pclient->fd_out != pclient->fd_in
		&& close(pclient->fd_out) < 0)
		i_error("close(%s/out) failed: %m", pclient->path);
	pclient->fd_in = pclient->fd_out = -1;
	
	pclient->disconnected = TRUE;
	if (error && pclient->error == PROGRAM_CLIENT_ERROR_NONE ) {
		pclient->error = PROGRAM_CLIENT_ERROR_UNKNOWN;
	}
}

void program_client_fail
(struct program_client *pclient, enum program_client_error error)
{
	if ( pclient->error != PROGRAM_CLIENT_ERROR_NONE )
		return;

	pclient->error = error;
	program_client_disconnect(pclient, TRUE);

	pclient->failure(pclient, error);
}

static int program_client_program_output(struct program_client *pclient)
{
	struct istream *input = pclient->input;
	struct ostream *output = pclient->program_output;
	const unsigned char *data;
	size_t size;
	int ret = 0;

	if ((ret = o_stream_flush(output)) <= 0) {
		if (ret < 0)
			program_client_fail(pclient, PROGRAM_CLIENT_ERROR_IO);
		return ret;
	}

	if ( input != NULL && output != NULL ) {
		do {
			while ( (data=i_stream_get_data(input, &size)) != NULL ) {
				ssize_t sent;
	
				if ( (sent=o_stream_send(output, data, size)) < 0 ) {
					program_client_fail(pclient, PROGRAM_CLIENT_ERROR_IO);
					return -1;
				}
	
				if ( sent == 0 )
					return 0;
				i_stream_skip(input, sent);
			}
		} while ( (ret=i_stream_read(input)) > 0 );

		if ( ret == 0 )
			return 1;

		if ( ret < 0 ) {
			if ( !input->eof ) {
				program_client_fail(pclient, PROGRAM_CLIENT_ERROR_IO);
				return -1;
			} else if ( !i_stream_have_bytes_left(input) ) {
				i_stream_unref(&pclient->input);
				input = NULL;

				if ( (ret = o_stream_flush(output)) <= 0 ) {
					if ( ret < 0 )
						program_client_fail(pclient, PROGRAM_CLIENT_ERROR_IO);
					return ret;
				}
			} 
		}
	}

	if ( input == NULL ) {
		if ( pclient->program_input == NULL ) {
			program_client_disconnect(pclient, FALSE);
		} else if (program_client_close_output(pclient) < 0) {
			program_client_fail(pclient, PROGRAM_CLIENT_ERROR_IO);
		}
	}
	return 1;
}

static void program_client_program_input(struct program_client *pclient)
{
	struct istream *input = pclient->program_input;
	struct ostream *output = pclient->output;
	const unsigned char *data;
	size_t size;
	int ret = 0;

	if ( input != NULL ) {
		while ( (ret=i_stream_read_data(input, &data, &size, 0)) > 0 ) {
			if ( output != NULL ) {
				ssize_t sent;

				if ( (sent=o_stream_send(output, data, size)) < 0 ) {
					program_client_fail(pclient, PROGRAM_CLIENT_ERROR_IO);
					return;
				}
				size = (size_t)sent;
			}

			i_stream_skip(input, size);
		}

		if ( ret < 0 ) {
			if ( input->eof ) {
				program_client_disconnect(pclient, FALSE);
				return;
			}
			program_client_fail(pclient, PROGRAM_CLIENT_ERROR_IO);
		}
	}
}

int program_client_connected
(struct program_client *pclient)
{
	int ret = 1;

	pclient->start_time = ioloop_time;
	if (pclient->to != NULL)
		timeout_remove(&pclient->to);
	if ( pclient->set->input_idle_timeout_secs != 0 ) {
		pclient->to = timeout_add(pclient->set->input_idle_timeout_secs*1000,
      program_client_timeout, pclient);
	}

	/* run output */
	if ( pclient->program_output != NULL &&
		(ret=program_client_program_output(pclient)) == 0 ) {
		if ( pclient->program_output != NULL ) {
			o_stream_set_flush_callback
				(pclient->program_output, program_client_program_output, pclient);
		}
	}

	return ret;
}

void program_client_init
(struct program_client *pclient, pool_t pool, const char *path,
	const char *const *args, const struct program_client_settings *set)
{
	pclient->pool = pool;
	pclient->path = p_strdup(pool, path);
	if ( args != NULL )
		pclient->args = p_strarray_dup(pool, args);
	pclient->set = set;
	pclient->debug = set->debug;
	pclient->fd_in = -1;
	pclient->fd_out = -1;
}

void program_client_set_input
(struct program_client *pclient, struct istream *input)
{
	if ( pclient->input )
		i_stream_unref(&pclient->input);
	if ( input != NULL )
		i_stream_ref(input);
	pclient->input = input;
}

void program_client_set_output
(struct program_client *pclient, struct ostream *output)
{
	if ( pclient->output )
		o_stream_unref(&pclient->output);
	if ( output != NULL )
		o_stream_ref(output);
	pclient->output = output;
}

void program_client_set_env
(struct program_client *pclient, const char *name, const char *value)
{
	const char *env;

	if ( !array_is_created(&pclient->envs) )
		p_array_init(&pclient->envs, pclient->pool, 16);

	env = p_strdup_printf(pclient->pool, "%s=%s", name, value);
	array_append(&pclient->envs, &env, 1);
}

void program_client_init_streams(struct program_client *pclient)
{
	if ( pclient->fd_out >= 0 ) {
		pclient->program_output =
			o_stream_create_fd(pclient->fd_out, MAX_OUTBUF_SIZE, FALSE);
	}
	if ( pclient->fd_in >= 0 ) {
		pclient->program_input =
			i_stream_create_fd(pclient->fd_in, (size_t)-1, FALSE);
		pclient->io = io_add
			(pclient->fd_in, IO_READ, program_client_program_input, pclient);
	}
}

void program_client_destroy(struct program_client **_pclient)
{
	struct program_client *pclient = *_pclient;

	program_client_disconnect(pclient, TRUE);

	if ( pclient->input != NULL )
		i_stream_unref(&pclient->input);
	if ( pclient->output != NULL )
		o_stream_unref(&pclient->output);
	if ( pclient->io != NULL )
		io_remove(&pclient->io);
	if ( pclient->ioloop != NULL )
		io_loop_destroy(&pclient->ioloop);

	pool_unref(&pclient->pool);
	*_pclient = NULL;
}

int program_client_run(struct program_client *pclient)
{
	int ret;

	pclient->ioloop = io_loop_create();

	if ( program_client_connect(pclient) >= 0 ) {
		/* run output */
		ret = 1;		
		if ( pclient->program_output != NULL &&
			(ret=o_stream_flush(pclient->program_output)) == 0 ) {
			o_stream_set_flush_callback
				(pclient->program_output, program_client_program_output, pclient);
		}

		/* run i/o event loop */
		if ( ret < 0 ) {
			pclient->error = PROGRAM_CLIENT_ERROR_IO;
		} else if ( pclient->io != NULL || ret == 0 ) {
			io_loop_run(pclient->ioloop);
		}

		/* finished */
		program_client_disconnect(pclient, FALSE);
	}

	io_loop_destroy(&pclient->ioloop);

	if ( pclient->error != PROGRAM_CLIENT_ERROR_NONE )
		return -1;

	return pclient->exit_code;
}

