/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "unichar.h"
#include "istream-private.h"
#include "ostream.h"
#include "strescape.h"
#include "managesieve-parser.h"

#define is_linebreak(c) \
	((c) == '\r' || (c) == '\n')

#define LIST_INIT_COUNT 7

enum arg_parse_type {
	ARG_PARSE_NONE = 0,
	ARG_PARSE_ATOM,
	ARG_PARSE_STRING,
	ARG_PARSE_LITERAL,
	ARG_PARSE_LITERAL_DATA
};

struct managesieve_parser {
	/* permanent */
	pool_t pool;
	struct istream *input;
	size_t max_line_size;
	enum managesieve_parser_flags flags;

	/* reset by managesieve_parser_reset(): */
	size_t line_size;
	ARRAY_TYPE(managesieve_arg_list) root_list;
	ARRAY_TYPE(managesieve_arg_list) *cur_list;
	struct managesieve_arg *list_arg;

	enum arg_parse_type cur_type;
	size_t cur_pos; /* parser position in input buffer */

	int str_first_escape; /* ARG_PARSE_STRING: index to first '\' */
	uoff_t literal_size; /* ARG_PARSE_LITERAL: string size */

	struct istream *str_stream;

	const char *error;

	unsigned int literal_skip_crlf:1;
	unsigned int literal_nonsync:1;
	unsigned int eol:1;
	unsigned int fatal_error:1;
};

static struct istream *quoted_string_istream_create
	(struct managesieve_parser *parser);

struct managesieve_parser *
managesieve_parser_create(struct istream *input, size_t max_line_size)
{
	struct managesieve_parser *parser;

	parser = i_new(struct managesieve_parser, 1);
	parser->pool = pool_alloconly_create("MANAGESIEVE parser", 8192);
	parser->input = input;
	parser->max_line_size = max_line_size;

	p_array_init(&parser->root_list, parser->pool, LIST_INIT_COUNT);
	parser->cur_list = &parser->root_list;
	return parser;
}

void managesieve_parser_destroy(struct managesieve_parser **parser)
{
	if ((*parser)->str_stream != NULL)
		i_stream_unref(&(*parser)->str_stream);

	pool_unref(&(*parser)->pool);
	i_free(*parser);
	*parser = NULL;
}

void managesieve_parser_reset(struct managesieve_parser *parser)
{
	p_clear(parser->pool);

	parser->line_size = 0;

	p_array_init(&parser->root_list, parser->pool, LIST_INIT_COUNT);
	parser->cur_list = &parser->root_list;
	parser->list_arg = NULL;

	parser->cur_type = ARG_PARSE_NONE;
	parser->cur_pos = 0;

	parser->str_first_escape = 0;
	parser->literal_size = 0;

	parser->error = NULL;

	parser->literal_skip_crlf = FALSE;
	parser->eol = FALSE;

	if ( parser->str_stream != NULL )
		i_stream_unref(&parser->str_stream);
}

const char *managesieve_parser_get_error
(struct managesieve_parser *parser, bool *fatal)
{
	*fatal = parser->fatal_error;
	return parser->error;
}

/* skip over everything parsed so far, plus the following whitespace */
static int managesieve_parser_skip_to_next(struct managesieve_parser *parser,
				    const unsigned char **data,
				    size_t *data_size)
{
	size_t i;

	for (i = parser->cur_pos; i < *data_size; i++) {
		if ((*data)[i] != ' ')
			break;
	}

	parser->line_size += i;
	i_stream_skip(parser->input, i);
	parser->cur_pos = 0;

	*data += i;
	*data_size -= i;
	return *data_size > 0;
}

static struct managesieve_arg *managesieve_arg_create
(struct managesieve_parser *parser)
{
	struct managesieve_arg *arg;

	arg = array_append_space(parser->cur_list);
	arg->parent = parser->list_arg;

	return arg;
}

static void managesieve_parser_save_arg(struct managesieve_parser *parser,
				 const unsigned char *data, size_t size)
{
	struct managesieve_arg *arg;
	char *str;

	arg = managesieve_arg_create(parser);

	switch (parser->cur_type) {
	case ARG_PARSE_ATOM:
		/* simply save the string */
		arg->type = MANAGESIEVE_ARG_ATOM;
		arg->_data.str = p_strndup(parser->pool, data, size);
		arg->str_len = size;
		break;
	case ARG_PARSE_STRING:
		/* data is quoted and may contain escapes. */
		if ((parser->flags & MANAGESIEVE_PARSE_FLAG_STRING_STREAM) != 0) {
			arg->type = MANAGESIEVE_ARG_STRING_STREAM;
			arg->_data.str_stream = parser->str_stream;
		} else {
			i_assert(size > 0);

			arg->type = MANAGESIEVE_ARG_STRING;
			str = p_strndup(parser->pool, data+1, size-1);

			/* remove the escapes */
			if (parser->str_first_escape >= 0 &&
				  (parser->flags & MANAGESIEVE_PARSE_FLAG_NO_UNESCAPE) == 0) {
				/* -1 because we skipped the '"' prefix */
				str_unescape(str + parser->str_first_escape-1);
			}

			arg->_data.str = str;
			arg->str_len = strlen(str);
		}
		break;
	case ARG_PARSE_LITERAL_DATA:
		if ((parser->flags & MANAGESIEVE_PARSE_FLAG_STRING_STREAM) != 0) {
			arg->type = MANAGESIEVE_ARG_STRING_STREAM;
			arg->_data.str_stream = parser->str_stream;
		} else if ((parser->flags & MANAGESIEVE_PARSE_FLAG_LITERAL_TYPE) != 0) {
			arg->type = MANAGESIEVE_ARG_LITERAL;
			arg->_data.str = p_strndup(parser->pool, data, size);
			arg->str_len = size;
		} else {
			arg->type = MANAGESIEVE_ARG_STRING;
			arg->_data.str = p_strndup(parser->pool, data, size);
			arg->str_len = size;
		}
		break;
	default:
		i_unreached();
	}

	parser->cur_type = ARG_PARSE_NONE;
}

static int is_valid_atom_char(struct managesieve_parser *parser, char chr)
{
	if (IS_ATOM_SPECIAL((unsigned char)chr)) {
		parser->error = "Invalid characters in atom";
		return FALSE;
	} else if ((chr & 0x80) != 0) {
		parser->error = "8bit data in atom";
		return FALSE;
	}

	return TRUE;
}

static int managesieve_parser_read_atom(struct managesieve_parser *parser,
				 const unsigned char *data, size_t data_size)
{
	size_t i;

	/* read until we've found space, CR or LF. */
	for (i = parser->cur_pos; i < data_size; i++) {
		if (data[i] == ' ' || data[i] == ')' ||
			 is_linebreak(data[i])) {
			managesieve_parser_save_arg(parser, data, i);
			break;
		} else if (!is_valid_atom_char(parser, data[i]))
			return FALSE;
	}

	parser->cur_pos = i;
	return parser->cur_type == ARG_PARSE_NONE;
}

static int managesieve_parser_read_string(struct managesieve_parser *parser,
				   const unsigned char *data, size_t data_size)
{
	size_t i;

	/* QUOTED-CHAR        = SAFE-UTF8-CHAR / "\" QUOTED-SPECIALS
	 * quoted             = <"> *QUOTED-CHAR <">
	 *                    ;; limited to 1024 octets between the <">s
	 */

	/* read until we've found non-escaped ", CR or LF */
	for (i = parser->cur_pos; i < data_size; i++) {
		if (data[i] == '"') {

			if ( !uni_utf8_data_is_valid(data+1, i-1) ) {
				parser->error = "Invalid UTF-8 character in quoted-string.";
				return FALSE;
			}

			managesieve_parser_save_arg(parser, data, i);
			i++; /* skip the trailing '"' too */
			break;
		}

		if (data[i] == '\\') {
			if (i+1 == data_size) {
				/* known data ends with '\' - leave it to
				   next time as well if it happens to be \" */
				break;
			}

			/* save the first escaped char */
			if (parser->str_first_escape < 0)
				parser->str_first_escape = i;

			/* skip the escaped char */
			i++;

			if ( !IS_QUOTED_SPECIAL(data[i]) ) {
				parser->error =
					"Escaped quoted-string character is not a QUOTED-SPECIAL.";
				return FALSE;
			}

			continue;
		}

		if ( (data[i] & 0x80) == 0 && !IS_SAFE_CHAR(data[i]) ) {
			parser->error = "String contains invalid character.";
			return FALSE;
		}
	}

	parser->cur_pos = i;
	return parser->cur_type == ARG_PARSE_NONE;
}

static int managesieve_parser_literal_end(struct managesieve_parser *parser)
{
	if ((parser->flags & MANAGESIEVE_PARSE_FLAG_STRING_STREAM) == 0) {
		if (parser->line_size >= parser->max_line_size ||
		    parser->literal_size >
		    	parser->max_line_size - parser->line_size) {
			/* too long string, abort. */
			parser->error = "Literal size too large";
			parser->fatal_error = TRUE;
			return FALSE;
		}
	}

	parser->cur_type = ARG_PARSE_LITERAL_DATA;
	parser->literal_skip_crlf = TRUE;

	parser->cur_pos = 0;
	return TRUE;
}

static int managesieve_parser_read_literal(struct managesieve_parser *parser,
				    const unsigned char *data,
				    size_t data_size)
{
	size_t i, prev_size;

	/* expecting digits + "}" */
	for (i = parser->cur_pos; i < data_size; i++) {
		if (data[i] == '}') {
			parser->line_size += i+1;
			i_stream_skip(parser->input, i+1);

			return managesieve_parser_literal_end(parser);
		}

		if (parser->literal_nonsync) {
			parser->error = "Expecting '}' after '+'";
			return FALSE;
		}

		if (data[i] == '+') {
			parser->literal_nonsync = TRUE;
			continue;
		}

		if (data[i] < '0' || data[i] > '9') {
			parser->error = "Invalid literal size";
			return FALSE;
		}

		prev_size = parser->literal_size;
		parser->literal_size = parser->literal_size*10 + (data[i]-'0');

		if (parser->literal_size < prev_size) {
			/* wrapped around, abort. */
			parser->error = "Literal size too large";
			return FALSE;
		}
	}

	parser->cur_pos = i;
	return FALSE;
}

static int managesieve_parser_read_literal_data(struct managesieve_parser *parser,
					 const unsigned char *data,
					 size_t data_size)
{
	if (parser->literal_skip_crlf) {

		/* skip \r\n or \n, anything else gives an error */
		if (data_size == 0)
			return FALSE;

		if (*data == '\r') {
			parser->line_size++;
			data++; data_size--;
			i_stream_skip(parser->input, 1);

			if (data_size == 0)
				return FALSE;
		}

		if (*data != '\n') {
			parser->error = "Missing LF after literal size";
			return FALSE;
		}

		parser->line_size++;
		data++; data_size--;
		i_stream_skip(parser->input, 1);

		parser->literal_skip_crlf = FALSE;

		i_assert(parser->cur_pos == 0);
	}

	if ((parser->flags & MANAGESIEVE_PARSE_FLAG_STRING_STREAM) == 0) {
		/* now we just wait until we've read enough data */
		if (data_size < parser->literal_size) {
			return FALSE;
		} else {
			if ( !uni_utf8_data_is_valid
				(data, (size_t)parser->literal_size) ) {
				parser->error = "Invalid UTF-8 character in literal string.";
				return FALSE;
			}

			managesieve_parser_save_arg(parser, data,
					     (size_t)parser->literal_size);
			parser->cur_pos = (size_t)parser->literal_size;
			return TRUE;
		}
	} else {
		/* we don't read the data; we just create a stream for the literal */
		parser->eol = TRUE;
		parser->str_stream = i_stream_create_limit
			(parser->input, parser->literal_size);
		managesieve_parser_save_arg(parser, NULL, 0);
		return TRUE;
	}
}

/* Returns TRUE if argument was fully processed. Also returns TRUE if
   an argument inside a list was processed. */
static int managesieve_parser_read_arg(struct managesieve_parser *parser)
{
	const unsigned char *data;
	size_t data_size;

	data = i_stream_get_data(parser->input, &data_size);
	if (data_size == 0)
		return FALSE;

	while (parser->cur_type == ARG_PARSE_NONE) {
		/* we haven't started parsing yet */
		if (!managesieve_parser_skip_to_next(parser, &data, &data_size))
			return FALSE;
		i_assert(parser->cur_pos == 0);

		switch (data[0]) {
		case '\r':
		case '\n':
			/* unexpected end of line */
			parser->eol = TRUE;
			return FALSE;
		case '"':
			parser->cur_type = ARG_PARSE_STRING;
			parser->str_first_escape = -1;
			break;
		case '{':
			parser->cur_type = ARG_PARSE_LITERAL;
			parser->literal_size = 0;
			parser->literal_nonsync = FALSE;
			break;
		default:
			if (!is_valid_atom_char(parser, data[0]))
				return FALSE;
			parser->cur_type = ARG_PARSE_ATOM;
			break;
		}

		parser->cur_pos++;
	}

	i_assert(data_size > 0);

	switch (parser->cur_type) {
	case ARG_PARSE_ATOM:
		if (!managesieve_parser_read_atom(parser, data, data_size))
			return FALSE;
		break;
	case ARG_PARSE_STRING:
		if ((parser->flags & MANAGESIEVE_PARSE_FLAG_STRING_STREAM) != 0) {
			parser->eol = TRUE;
			parser->line_size += parser->cur_pos;
			i_stream_skip(parser->input, parser->cur_pos);
			parser->cur_pos = 0;
			parser->str_stream = quoted_string_istream_create(parser);
			managesieve_parser_save_arg(parser, NULL, 0);

		} else if (!managesieve_parser_read_string(parser, data, data_size)) {
			return FALSE;
		}
		break;
	case ARG_PARSE_LITERAL:
		if (!managesieve_parser_read_literal(parser, data, data_size))
			return FALSE;

		/* pass through to parsing data. since input->skip was
		   modified, we need to get the data start position again. */
		data = i_stream_get_data(parser->input, &data_size);

		/* fall through */
	case ARG_PARSE_LITERAL_DATA:
		if (!managesieve_parser_read_literal_data(parser, data, data_size))
			return FALSE;
		break;
	default:
		i_unreached();
	}

	i_assert(parser->cur_type == ARG_PARSE_NONE);
	return TRUE;
}

/* ARG_PARSE_NONE checks that last argument isn't only partially parsed. */
#define IS_UNFINISHED(parser) \
	((parser)->cur_type != ARG_PARSE_NONE || \
	(parser)->cur_list != &parser->root_list)

static int finish_line
(struct managesieve_parser *parser, unsigned int count,
	const struct managesieve_arg **args_r)
{
	struct managesieve_arg *arg;
	int ret = array_count(&parser->root_list);

	parser->line_size += parser->cur_pos;
	i_stream_skip(parser->input, parser->cur_pos);
	parser->cur_pos = 0;

	/* fill the missing parameters with NILs */
	while (count > array_count(&parser->root_list)) {
		arg = array_append_space(&parser->root_list);
		arg->type = MANAGESIEVE_ARG_NONE;
	}
	arg = array_append_space(&parser->root_list);
	arg->type = MANAGESIEVE_ARG_EOL;

	*args_r = array_get(&parser->root_list, &count);
	return ret;
}

int managesieve_parser_read_args
(struct managesieve_parser *parser, unsigned int count,
	enum managesieve_parser_flags flags, const struct managesieve_arg **args_r)
{
	parser->flags = flags;

	while ( !parser->eol && (count == 0 || IS_UNFINISHED(parser)
		|| array_count(&parser->root_list) < count) ) {
		if ( !managesieve_parser_read_arg(parser) )
			break;

		if ( parser->line_size > parser->max_line_size ) {
			parser->error = "MANAGESIEVE command line too large";
			break;
		}
	}

	if ( parser->error != NULL ) {
		/* error, abort */
		parser->line_size += parser->cur_pos;
		i_stream_skip(parser->input, parser->cur_pos);
		parser->cur_pos = 0;
		*args_r = NULL;
		return -1;
	} else if ( (!IS_UNFINISHED(parser) && count > 0
		&& array_count(&parser->root_list) >= count) || parser->eol ) {
		/* all arguments read / end of line. */
		return finish_line(parser, count, args_r);
	} else {
		/* need more data */
		*args_r = NULL;
		return -2;
	}
}

int managesieve_parser_finish_line
(struct managesieve_parser *parser, unsigned int count,
	enum managesieve_parser_flags flags, const struct managesieve_arg **args_r)
{
	const unsigned char *data;
	size_t data_size;
	int ret;

	ret = managesieve_parser_read_args(parser, count, flags, args_r);
	if (ret == -2) {
		/* we should have noticed end of everything except atom */
		if (parser->cur_type == ARG_PARSE_ATOM) {
			data = i_stream_get_data(parser->input, &data_size);
			managesieve_parser_save_arg(parser, data, data_size);
		}
	}
	return finish_line(parser, count, args_r);
}

const char *managesieve_parser_read_word(struct managesieve_parser *parser)
{
	const unsigned char *data;
	size_t i, data_size;

	data = i_stream_get_data(parser->input, &data_size);

	for (i = 0; i < data_size; i++) {
		if (data[i] == ' ' || data[i] == '\r' || data[i] == '\n')
			break;
	}

	if (i < data_size) {
		data_size = i + (data[i] == ' ' ? 1 : 0);
		parser->line_size += data_size;
		i_stream_skip(parser->input, data_size);
		return p_strndup(parser->pool, data, i);
	} else {
		return NULL;
	}
}

/*
 * Quoted string stream
 */

struct quoted_string_istream {
	struct istream_private istream;

	struct stat statbuf;

	struct managesieve_parser *parser;

	unsigned int pending_slash:1;
	unsigned int last_slash:1;
	unsigned int finished:1;
};

static ssize_t quoted_string_istream_read(struct istream_private *stream)
{
	struct quoted_string_istream *qsstream =
		(struct quoted_string_istream *)stream;
	const unsigned char *data;
	size_t i, dest, size, avail;
	ssize_t ret = 0;
	bool slash;

	if ( qsstream->finished ) {
		stream->istream.eof = TRUE;
		return -1;
	}

	/* Read from parent */
	data = i_stream_get_data(stream->parent, &size);
	if (size == 0) {
		ret = i_stream_read(stream->parent);
		if (ret <= 0 && (ret != -2 || stream->skip == 0)) {
			if ( stream->istream.eof && stream->istream.stream_errno == 0 ) {
				stream->istream.eof = 0;
				stream->istream.stream_errno = EIO;
			} else {
				stream->istream.stream_errno = stream->parent->stream_errno;
				stream->istream.eof = stream->parent->eof;
			}
			return ret;
		}
		data = i_stream_get_data(stream->parent, &size);
		i_assert(size != 0);
	}

	/* Allocate buffer space */
	if (!i_stream_try_alloc(stream, size, &avail))
		return -2;

	/* Parse quoted string content */
	dest = stream->pos;
	slash = qsstream->pending_slash;
	ret = 0;
	for (i = 0; i < size && dest < stream->buffer_size; i++) {
		if ( data[i] == '"' ) {
			if ( !slash ) {
				qsstream->finished = TRUE;
				i++;
				break;
			}
			slash = FALSE;
		} else if ( data[i] == '\\' ) {
			if ( !slash ) {
				slash = TRUE;
				continue;
			}
			slash = FALSE;
		} else if ( slash ) {
			if ( !IS_QUOTED_SPECIAL(data[i]) ) {
				qsstream->parser->error =
					"Escaped quoted-string character is not a QUOTED-SPECIAL.";
				stream->istream.stream_errno = EIO;
				ret = -1;
				break;
			}
			slash = FALSE;
		}

		if ( (data[i] & 0x80) == 0 && ( data[i] == '\r' || data[i] == '\n' ) ) {
			qsstream->parser->error = "String contains invalid character.";
			stream->istream.stream_errno = EIO;
			ret = -1;
			break;
		}

		stream->w_buffer[dest++] = data[i];
	}

	i_stream_skip(stream->parent, i);
	qsstream->pending_slash = slash;

	if ( ret < 0 ) {
		stream->pos = dest;
		return ret;
	}

	ret = dest - stream->pos;
	if (ret == 0) {
		if ( qsstream->finished ) {
			stream->istream.eof = TRUE;
			return -1;
		}
		i_assert(qsstream->pending_slash && size == 1);
		return quoted_string_istream_read(stream);
	}
	i_assert(ret > 0);
	stream->pos = dest;
	return ret;
}

static int quoted_string_istream_stat
(struct istream_private *stream, bool exact)
{
	const struct stat *st;

	if (i_stream_stat(stream->parent, exact, &st) < 0)
		return -1;

	stream->statbuf = *st;
	return 0;
}

static struct istream *quoted_string_istream_create
(struct managesieve_parser *parser)
{
	struct quoted_string_istream *qsstream;

	qsstream = i_new(struct quoted_string_istream, 1);
	qsstream->parser = parser;

	qsstream->istream.max_buffer_size =
		parser->input->real_stream->max_buffer_size;

	qsstream->istream.read = quoted_string_istream_read;
	qsstream->istream.stat = quoted_string_istream_stat;

	qsstream->istream.istream.readable_fd = FALSE;
	qsstream->istream.istream.blocking = parser->input->blocking;
	qsstream->istream.istream.seekable = FALSE;
	return i_stream_create(&qsstream->istream, parser->input,
			       i_stream_get_fd(parser->input));
}




