/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#ifndef __MANAGESIEVE_PARSER_H
#define __MANAGESIEVE_PARSER_H

#include "managesieve-arg.h"

enum managesieve_parser_flags {
	/* Set this flag if you want to read a string argument as as stream. Useful
	   when you need to deal with large strings. The string must be the last read
	   argument. */
	MANAGESIEVE_PARSE_FLAG_STRING_STREAM = 0x01,
	/* Don't remove '\' chars from string arguments */
	MANAGESIEVE_PARSE_FLAG_NO_UNESCAPE	= 0x02,
	/* Return literals as MANAGESIEVE_ARG_LITERAL instead of
	   MANAGESIEVE_ARG_STRING */
	MANAGESIEVE_PARSE_FLAG_LITERAL_TYPE	= 0x04
};

struct managesieve_parser;

/* Create new MANAGESIEVE argument parser.

   max_line_size can be used to approximately limit the maximum amount of
   memory that gets allocated when parsing a line. Input buffer size limits
   the maximum size of each parsed token.

   Usually the largest lines are large only because they have a one huge
   message set token, so you'll probably want to keep input buffer size the
   same as max_line_size. That means the maximum memory usage is around
   2 * max_line_size. */
struct managesieve_parser *
managesieve_parser_create(struct istream *input, size_t max_line_size);
void managesieve_parser_destroy(struct managesieve_parser **parser);

/* Reset the parser to initial state. */
void managesieve_parser_reset(struct managesieve_parser *parser);

/* Return the last error in parser. fatal is set to TRUE if there's no way to
   continue parsing, currently only if too large non-sync literal size was
   given. */
const char *managesieve_parser_get_error
	(struct managesieve_parser *parser, bool *fatal);

/* Read a number of arguments. This function doesn't call i_stream_read(), you
   need to do that. Returns number of arguments read (may be less than count
   in case of EOL), -2 if more data is needed or -1 if error occurred.

   count-sized array of arguments are stored into args when return value is
   0 or larger. If all arguments weren't read, they're set to NIL. count
   can be set to 0 to read all arguments in the line. Last element in
   args is always of type MANAGESIEVE_ARG_EOL. */
int managesieve_parser_read_args
	(struct managesieve_parser *parser, unsigned int count,
		enum managesieve_parser_flags flags, const struct managesieve_arg **args_r);

/* just like managesieve_parser_read_args(), but assume \n at end of data in
   input stream. */
int managesieve_parser_finish_line
	(struct managesieve_parser *parser, unsigned int count,
		enum managesieve_parser_flags flags, const struct managesieve_arg **args_r);

/* Read one word - used for reading command name.
   Returns NULL if more data is needed. */
const char *managesieve_parser_read_word(struct managesieve_parser *parser);

#endif
