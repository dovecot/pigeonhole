/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "ioloop.h"
#include "str.h"
#include "net.h"
#include "write-full.h"
#include "eacces-error.h"
#include "istream-private.h"
#include "ostream.h"

#include "script-client-private.h"

#include <unistd.h>
#include <sys/wait.h>
#include <sysexits.h>

/*
 * Script client input stream
 */

struct script_client_istream {
	struct istream_private istream;

	struct stat statbuf;

	struct script_client *client;
};

static void script_client_istream_destroy(struct iostream_private *stream)
{
	struct script_client_istream *scstream =
		(struct script_client_istream *)stream;

	i_stream_unref(&scstream->istream.parent);
}

static ssize_t script_client_istream_read(struct istream_private *stream)
{
	struct script_client_istream *scstream =
		(struct script_client_istream *)stream;
	size_t pos, reserved;
	ssize_t ret;

	i_stream_skip(stream->parent, stream->skip);
	stream->pos -= stream->skip;
	stream->skip = 0;

	stream->buffer = i_stream_get_data(stream->parent, &pos);

	if ( stream->buffer != NULL && pos >= 1 ) {
		/* retain/hide potential return code at end of buffer */
		reserved = ( stream->buffer[pos-1] == '\n' && pos > 1 ? 2 : 1 );
		pos -= reserved;
	}

	if (pos > stream->pos) {
		ret = 0;
	} else if ( stream->parent->eof ) {
		stream->istream.eof = TRUE;
		ret = -1;
	} else do {
		if ((ret = i_stream_read(stream->parent)) == -2)
			return -2; /* input buffer full */

		stream->istream.stream_errno = stream->parent->stream_errno;
		stream->buffer = i_stream_get_data(stream->parent, &pos);

		if ( stream->parent->eof ) {
			/* Check return code at EOF */
			if ( stream->buffer != NULL && pos >= 2 &&
				stream->buffer[pos-1] == '\n' ) {
				switch ( stream->buffer[pos-2] ) {
				case '+':
					scstream->client->exit_code = 1;
					break;
				case '-':
					scstream->client->exit_code = 0;
				default:
					scstream->client->exit_code = -1;
				}
			} else {
				scstream->client->exit_code = -1;
			}
		}
	
		if ( ret == 0 || (ret < 0 && !stream->parent->eof) ) break;

		if ( stream->buffer != NULL && pos >= 1 ) {
			/* retain/hide potential return code at end of buffer */
			reserved = ( stream->buffer[pos-1] == '\n' && pos > 1 ? 2 : 1 );

			pos -= reserved;

			if ( ret > 0 ) {
				ret = ( (size_t)ret > reserved ? ret - reserved : 0 );
			}
		}

		if ( ret <= 0 && stream->parent->eof ) {
			/* Parent EOF and not more data to return; EOF here as well */
			stream->istream.eof = TRUE;
			ret = -1;
		}		
	} while ( ret == 0 );

 	ret = pos > stream->pos ? (ssize_t)(pos - stream->pos) : (ret == 0 ? 0 : -1);
	stream->pos = pos;

	i_assert(ret != -1 || stream->istream.eof ||
		stream->istream.stream_errno != 0);

	return ret;
}

static void ATTR_NORETURN script_client_istream_sync
(struct istream_private *stream ATTR_UNUSED)
{
	i_panic("script_client_istream sync() not implemented");
}

static int script_client_istream_stat
(struct istream_private *stream, bool exact)
{
	struct script_client_istream *scstream =
		(struct script_client_istream *)stream;
	const struct stat *st;
	int ret;

	/* Stat the original stream */
	ret = i_stream_stat(stream->parent, exact, &st);
	if (ret < 0 || st->st_size == -1 || !exact)
		return ret;

	scstream->statbuf = *st;
	scstream->statbuf.st_size = -1;

	return ret;
}

static struct istream *script_client_istream_create
(struct script_client *script_client, struct istream *input)
{
	struct script_client_istream *scstream;

	scstream = i_new(struct script_client_istream, 1);
	scstream->client = script_client;

	scstream->istream.max_buffer_size = input->real_stream->max_buffer_size;

	scstream->istream.iostream.destroy = script_client_istream_destroy;
	scstream->istream.read = script_client_istream_read;
	scstream->istream.sync = script_client_istream_sync;
	scstream->istream.stat = script_client_istream_stat;

	scstream->istream.istream.readable_fd = FALSE;
	scstream->istream.istream.blocking = input->blocking;
	scstream->istream.istream.seekable = FALSE;

	i_stream_seek(input, 0);

	return i_stream_create(&scstream->istream, input, -1);
}

/*
 * Script client
 */

struct script_client_remote {
	struct script_client client;

	unsigned int noreply:1;
};

static void script_client_remote_connected(struct script_client *sclient)
{
	struct script_client_remote *slclient =
		(struct script_client_remote *)sclient;
	const char **args = sclient->args;
	string_t *str;

	io_remove(&sclient->io);
	script_client_init_streams(sclient);

	if ( !slclient->noreply ) {
		sclient->script_input = script_client_istream_create
			(sclient, sclient->script_input);
	}

	str = t_str_new(1024);
	str_append(str, "VERSION\tscript\t3\t0\n");
	if ( slclient->noreply )
		str_append(str, "noreply\n");
	else
		str_append(str, "-\n");
	if ( args != NULL ) {
		for (; *args != NULL; args++) {
			str_append(str, *args);
			str_append_c(str, '\n');
		}
	}
	str_append_c(str, '\n');

	if ( o_stream_send
		(sclient->script_output, str_data(str), str_len(str)) < 0 ) {
		script_client_fail(sclient, SCRIPT_CLIENT_ERROR_IO);
		return;
	}
	
	(void)script_client_script_connected(sclient);
}

static int script_client_remote_connect(struct script_client *sclient)
{
	struct script_client_remote *slclient =
		(struct script_client_remote *)sclient;
	int fd;

	if ((fd = net_connect_unix(sclient->path)) < 0) {
		switch (errno) {
		case EAGAIN:
		case ECONNREFUSED:
			// FIXME: retry;
			return -1;
		case EACCES:
			i_error("%s", eacces_error_get("net_connect_unix", sclient->path));
			return -1;
		default:
			i_error("net_connect_unix(%s) failed: %m", sclient->path);
			return -1;
		}
	}

	net_set_nonblock(fd, TRUE);
	
	sclient->fd_in = ( slclient->noreply && sclient->output == NULL ? -1 : fd );
	sclient->fd_out = fd;
	sclient->io = io_add(fd, IO_WRITE, script_client_remote_connected, sclient);
	return 1;
}

static int script_client_remote_close_output(struct script_client *sclient)
{
	/* Shutdown output; script stdin will get EOF */
	if ( shutdown(sclient->fd_out, SHUT_WR) < 0 ) {
		i_error("shutdown(%s, SHUT_WR) failed: %m", sclient->path);
		return -1;
	}

	return 1;
}

static int script_client_remote_disconnect
(struct script_client *sclient, bool force)
{
	struct script_client_remote *slclient =
		(struct script_client_remote *)sclient;
	int ret = 0;
	
	if ( sclient->error == SCRIPT_CLIENT_ERROR_NONE && !slclient->noreply &&
		sclient->script_input != NULL && !force) {
		const unsigned char *data;
		size_t size;

		/* Skip any remaining script output and parse the exit code */
		while ((ret = i_stream_read_data
			(sclient->script_input, &data, &size, 0)) > 0) {	
			i_stream_skip(sclient->script_input, size);
		}

		/* Get exit code */
		if ( !sclient->script_input->eof )
			ret = -1;
		else
			ret = sclient->exit_code;		
	} else {
		ret = 1;
	}

	return ret;
}

static void script_client_remote_failure
(struct script_client *sclient, enum script_client_error error)
{
	switch ( error ) {
	case SCRIPT_CLIENT_ERROR_CONNECT_TIMEOUT:
		i_error("program `%s' socket connection timed out (> %d msecs)",
			sclient->path, sclient->set->client_connect_timeout_msecs);
		break;
	case SCRIPT_CLIENT_ERROR_RUN_TIMEOUT:
		i_error("program `%s' execution timed out (> %d secs)",
			sclient->path, sclient->set->input_idle_timeout_secs);
		break;
	default:
		break;
	}
}

struct script_client *script_client_remote_create
(const char *socket_path, const char *const *args, 
	const struct script_client_settings *set, bool noreply)
{
	struct script_client_remote *sclient;
	pool_t pool;

	pool = pool_alloconly_create("script client remote", 1024);
	sclient = p_new(pool, struct script_client_remote, 1);
	script_client_init(&sclient->client, pool, socket_path, args, set);
	sclient->client.connect = script_client_remote_connect;
	sclient->client.close_output = script_client_remote_close_output;
	sclient->client.disconnect = script_client_remote_disconnect;
	sclient->client.failure = script_client_remote_failure;
	sclient->noreply = noreply;

	return &sclient->client;
}

