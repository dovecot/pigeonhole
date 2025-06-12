/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "compat.h"
#include "str.h"
#include "str-sanitize.h"
#include "istream.h"

#include "sieve-common.h"
#include "sieve-limits.h"
#include "sieve-error.h"
#include "sieve-script.h"

#include "sieve-lexer.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>

/*
 * Useful macros
 */

#define DIGIT_VAL(c) (c - '0')

/*
 * Lexer object
 */

struct sieve_lexical_scanner {
	pool_t pool;
	struct sieve_instance *svinst;

	struct sieve_script *script;
	struct istream *input;

	struct sieve_error_handler *ehandler;

	/* Currently scanned data */
	const unsigned char *buffer;
	size_t buffer_size;
	size_t buffer_pos;

	struct sieve_lexer lexer;

	int current_line;
};

const struct sieve_lexer *
sieve_lexer_create(struct sieve_script *script,
		   struct sieve_error_handler *ehandler,
		   enum sieve_error *error_code_r)
{
	struct sieve_lexical_scanner *scanner;
	struct sieve_instance *svinst = sieve_script_svinst(script);
	struct istream *stream;
	const struct stat *st;

	/* Open script as stream */
	if (sieve_script_get_stream(script, &stream, error_code_r) < 0)
		return NULL;

	/* Check script size */
	if (i_stream_stat(stream, TRUE, &st) >= 0 && st->st_size > 0 &&
	    svinst->set->max_script_size > 0 &&
	    (uoff_t)st->st_size > svinst->set->max_script_size) {
		sieve_error(ehandler, sieve_script_name(script),
			"sieve script is too large (max %zu bytes)",
			svinst->set->max_script_size);
		if (error_code_r != NULL)
			*error_code_r = SIEVE_ERROR_NOT_POSSIBLE;
		return NULL;
	}

	scanner = i_new(struct sieve_lexical_scanner, 1);
	scanner->lexer.scanner = scanner;

	scanner->ehandler = ehandler;
	sieve_error_handler_ref(ehandler);

	scanner->input = stream;
	i_stream_ref(scanner->input);

	scanner->script = script;
	sieve_script_ref(script);

	scanner->buffer = NULL;
	scanner->buffer_size = 0;
	scanner->buffer_pos = 0;

	scanner->lexer.token_type = STT_NONE;
	scanner->lexer.token_str_value = str_new(default_pool, 256);
	scanner->lexer.token_int_value = 0;
	scanner->lexer.token_line = 1;

	scanner->current_line = 1;

	return &scanner->lexer;
}

void sieve_lexer_free(const struct sieve_lexer **_lexer)
{
	const struct sieve_lexer *lexer = *_lexer;
	struct sieve_lexical_scanner *scanner = lexer->scanner;

	i_stream_unref(&scanner->input);
	sieve_script_unref(&scanner->script);
	sieve_error_handler_unref(&scanner->ehandler);
	str_free(&scanner->lexer.token_str_value);

	i_free(scanner);
	*_lexer = NULL;
}

/*
 * Internal error handling
 */

inline static void ATTR_FORMAT(4, 5)
sieve_lexer_error(const struct sieve_lexer *lexer,
		  const char *csrc_filename, unsigned int csrc_linenum,
		  const char *fmt, ...)
{
	struct sieve_lexical_scanner *scanner = lexer->scanner;
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_ERROR,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
	};
	va_list args;

	va_start(args, fmt);

	T_BEGIN {
		params.location =
			sieve_error_script_location(scanner->script,
						    scanner->current_line);
		sieve_logv(scanner->ehandler, &params, fmt, args);
	} T_END;

	va_end(args);
}
#define sieve_lexer_error(lexer, ...) \
	sieve_lexer_error(lexer, __FILE__, __LINE__, __VA_ARGS__)

inline static void ATTR_FORMAT(4, 5)
sieve_lexer_warning(const struct sieve_lexer *lexer,
		    const char *csrc_filename, unsigned int csrc_linenum,
		    const char *fmt, ...)
{
	struct sieve_lexical_scanner *scanner = lexer->scanner;
	struct sieve_error_params params = {
		.log_type = LOG_TYPE_WARNING,
		.csrc = {
			.filename = csrc_filename,
			.linenum = csrc_linenum,
		},
	};
	va_list args;

	va_start(args, fmt);

	T_BEGIN {
		params.location =
			sieve_error_script_location(scanner->script,
						    scanner->current_line);
		sieve_logv(scanner->ehandler, &params, fmt, args);
	} T_END;

	va_end(args);
}
#define sieve_lexer_warning(lexer, ...) \
	sieve_lexer_warning(lexer, __FILE__, __LINE__, __VA_ARGS__)

const char *sieve_lexer_token_description(const struct sieve_lexer *lexer)
{
	switch (lexer->token_type) {
	case STT_NONE:
		return "no token (bug)";
	case STT_WHITESPACE:
		return "whitespace (bug)";
	case STT_EOF:
		return "end of file";

	case STT_NUMBER:
		return "number";
	case STT_IDENTIFIER:
		return "identifier";
	case STT_TAG:
		return "tag";
	case STT_STRING:
		return "string";

	case STT_RBRACKET:
		return "')'";
	case STT_LBRACKET:
		return "'('";
	case STT_RCURLY:
		return "'}'";
	case STT_LCURLY:
		return "'{'";
	case STT_RSQUARE:
		return "']'";
	case STT_LSQUARE:
		return "'['";
	case STT_SEMICOLON:
		return "';'";
	case STT_COMMA:
		return "','";

	case STT_SLASH:
		return "'/'";
	case STT_COLON:
		return "':'";

	case STT_GARBAGE:
		return "unknown characters";
	case STT_ERROR:
		return "error token (bug)";
	}

	return "unknown token (bug)";
}

/*
 * Debug
 */

void sieve_lexer_token_print(const struct sieve_lexer *lexer)
{
	switch (lexer->token_type) {
	case STT_NONE:
		printf("??NONE?? ");
		break;
	case STT_WHITESPACE:
		printf("??WHITESPACE?? ");
		break;
	case STT_EOF:
		printf("EOF\n");
		break;

	case STT_NUMBER:
		printf("NUMBER ");
		break;
	case STT_IDENTIFIER:
		printf("IDENTIFIER ");
		break;
	case STT_TAG:
		printf("TAG ");
		break;
	case STT_STRING:
		printf("STRING ");
		break;

	case STT_RBRACKET:
		printf(") ");
		break;
	case STT_LBRACKET:
		printf("( ");
		break;
	case STT_RCURLY:
		printf("}\n");
		break;
	case STT_LCURLY:
		printf("{\n");
		break;
	case STT_RSQUARE:
		printf("] ");
		break;
	case STT_LSQUARE:
		printf("[ ");
		break;
	case STT_SEMICOLON:
		printf(";\n");
		break;
	case STT_COMMA:
		printf(", ");
		break;

	case STT_SLASH:
		printf("/ ");
		break;
	case STT_COLON:
		printf(": ");
		break;

	case STT_GARBAGE:
		printf(">>GARBAGE<<");
		break;
	case STT_ERROR:
		printf(">>ERROR<<");
		break;
	default:
		printf("UNKNOWN ");
		break;
	}
}

/*
 * Lexical scanning
 */

static void sieve_lexer_shift(struct sieve_lexical_scanner *scanner)
{
	if (scanner->buffer_size > 0 &&
	    scanner->buffer[scanner->buffer_pos] == '\n')
		scanner->current_line++;

	if (scanner->buffer_size > 0 &&
	    scanner->buffer_pos + 1 < scanner->buffer_size)
		scanner->buffer_pos++;
	else {
		if (scanner->buffer_size > 0)
			i_stream_skip(scanner->input, scanner->buffer_size);

		scanner->buffer = i_stream_get_data(scanner->input,
						    &scanner->buffer_size);

		if (scanner->buffer_size == 0 &&
		    i_stream_read(scanner->input) > 0) {
	  		scanner->buffer = i_stream_get_data(
				scanner->input, &scanner->buffer_size);
		}

		scanner->buffer_pos = 0;
	}
}

static inline int sieve_lexer_curchar(struct sieve_lexical_scanner *scanner)
{
	if (scanner->buffer_size == 0)
		return -1;

	return scanner->buffer[scanner->buffer_pos];
}

static inline const char *_char_sanitize(int ch)
{
	if (ch > 31 && ch < 127)
		return t_strdup_printf("'%c'", ch);

	return t_strdup_printf("0x%02x", ch);
}

static bool sieve_lexer_scan_number(struct sieve_lexical_scanner *scanner)
{
	struct sieve_lexer *lexer = &scanner->lexer;
	uintmax_t value;
	string_t *str;
	bool overflow = FALSE;

	str_truncate(lexer->token_str_value,0);
	str = lexer->token_str_value;

	while (i_isdigit(sieve_lexer_curchar(scanner))) {
		str_append_c(str, sieve_lexer_curchar(scanner));
		sieve_lexer_shift(scanner);
	}

	if (str_to_uintmax(str_c(str), &value) < 0 ||
	    value > (sieve_number_t)-1) {
		overflow = TRUE;
	} else {
		switch (sieve_lexer_curchar(scanner)) {
		case 'k':
		case 'K': /* Kilo */
			if (value > (SIEVE_MAX_NUMBER >> 10))
				overflow = TRUE;
			else
				value = value << 10;
			sieve_lexer_shift(scanner);
			break;
		case 'm':
		case 'M': /* Mega */
			if (value > (SIEVE_MAX_NUMBER >> 20))
				overflow = TRUE;
			else
				value = value << 20;
			sieve_lexer_shift(scanner);
			break;
		case 'g':
		case 'G': /* Giga */
			if (value > (SIEVE_MAX_NUMBER >> 30))
				overflow = TRUE;
			else
				value = value << 30;
			sieve_lexer_shift(scanner);
			break;
		default:
			/* Next token */
			break;
		}
	}

	/* Check for integer overflow */
	if (overflow) {
		sieve_lexer_error(lexer,
				  "number exceeds integer limits (max %llu)",
				  (long long) SIEVE_MAX_NUMBER);
		lexer->token_type = STT_ERROR;
		return FALSE;
	}

	lexer->token_type = STT_NUMBER;
	lexer->token_int_value = (sieve_number_t)value;
	return TRUE;

}

static bool
sieve_lexer_scan_hash_comment(struct sieve_lexical_scanner *scanner)
{
	struct sieve_lexer *lexer = &scanner->lexer;

	while (sieve_lexer_curchar(scanner) != '\n') {
		switch(sieve_lexer_curchar(scanner)) {
		case -1:
			if (!scanner->input->eof) {
				lexer->token_type = STT_ERROR;
				return FALSE;
			}
			sieve_lexer_warning(lexer,
				"no newline (CRLF) at end of hash comment at end of file");
			lexer->token_type = STT_WHITESPACE;
			return TRUE;
		case '\0':
			sieve_lexer_error(lexer,
					  "encountered NUL character in hash comment");
			lexer->token_type = STT_ERROR;
			return FALSE;
		default:
			break;
		}

		/* Stray CR is ignored */
		sieve_lexer_shift(scanner);
	}

	sieve_lexer_shift(scanner);

	lexer->token_type = STT_WHITESPACE;
	return TRUE;
}

/* sieve_lexer_scan_raw_token:
 *   Scans valid tokens and whitespace
 */
static bool
sieve_lexer_scan_raw_token(struct sieve_lexical_scanner *scanner)
{
	struct sieve_lexer *lexer = &scanner->lexer;
	string_t *str;
	int ret;

	/* Read first character */
	if (lexer->token_type == STT_NONE) {
		if ((ret = i_stream_read(scanner->input)) < 0) {
			i_assert(ret != -2);
			if (!scanner->input->eof) {
				lexer->token_type = STT_ERROR;
				return FALSE;
			}
		}
		sieve_lexer_shift(scanner);
	}

	lexer->token_line = scanner->current_line;

	switch (sieve_lexer_curchar(scanner)) {

	/* whitespace */

	// hash-comment = ( "#" *CHAR-NOT-CRLF CRLF )
	case '#':
		sieve_lexer_shift(scanner);
		return sieve_lexer_scan_hash_comment(scanner);

	// bracket-comment = "/*" *(CHAR-NOT-STAR / ("*" CHAR-NOT-SLASH)) "*/"
	//        ;; No */ allowed inside a comment.
	//        ;; (No * is allowed unless it is the last character,
	//        ;; or unless it is followed by a character that isn't a
	//        ;; slash.)
	case '/':
		sieve_lexer_shift(scanner);

		if (sieve_lexer_curchar(scanner) == '*') {
			sieve_lexer_shift(scanner);

			while (TRUE) {
				switch (sieve_lexer_curchar(scanner)) {
				case -1:
					if (scanner->input->eof) {
						sieve_lexer_error(lexer,
							"end of file before end of bracket comment "
							"('/* ... */') "
							"started at line %d",
							lexer->token_line);
					}
					lexer->token_type = STT_ERROR;
					return FALSE;
				case '*':
					sieve_lexer_shift(scanner);

					if (sieve_lexer_curchar(scanner) == '/') {
						sieve_lexer_shift(scanner);

						lexer->token_type = STT_WHITESPACE;
						return TRUE;

					} else if (sieve_lexer_curchar(scanner) == -1) {
						sieve_lexer_error(lexer,
							"end of file before end of bracket comment "
							"('/* ... */') "
							"started at line %d",
							lexer->token_line);
						lexer->token_type = STT_ERROR;
						return FALSE;
					}
					break;
				case '\0':
					sieve_lexer_error(lexer,
						"encountered NUL character in bracket comment");
					lexer->token_type = STT_ERROR;
					return FALSE;
				default:
					sieve_lexer_shift(scanner);
				}
			}

			i_unreached();
		}

		lexer->token_type = STT_SLASH;
		return TRUE;

	// comment = bracket-comment / hash-comment
	// white-space = 1*(SP / CRLF / HTAB) / comment
	case '\t':
	case '\r':
	case '\n':
	case ' ':
		sieve_lexer_shift(scanner);

		while (sieve_lexer_curchar(scanner) == '\t' ||
		       sieve_lexer_curchar(scanner) == '\r' ||
		       sieve_lexer_curchar(scanner) == '\n' ||
		       sieve_lexer_curchar(scanner) == ' ') {

			sieve_lexer_shift(scanner);
		}

		lexer->token_type = STT_WHITESPACE;
		return TRUE;

	/* quoted-string */
	case '"':
		sieve_lexer_shift(scanner);

		str_truncate(lexer->token_str_value, 0);
		str = lexer->token_str_value;

		while (sieve_lexer_curchar(scanner) != '"') {
			if (sieve_lexer_curchar(scanner) == '\\')
				sieve_lexer_shift(scanner);

			switch (sieve_lexer_curchar(scanner)) {

			/* End of file */
			case -1:
				if (scanner->input->eof) {
					sieve_lexer_error(lexer,
						"end of file before end of quoted string "
						"started at line %d", lexer->token_line);
				}
				lexer->token_type = STT_ERROR;
				return FALSE;

			/* NUL character */
			case '\0':
				sieve_lexer_error(lexer,
					"encountered NUL character in quoted string "
					"started at line %d", lexer->token_line);
				lexer->token_type = STT_ERROR;
				return FALSE;

			/* CR .. check for LF */
			case '\r':
				sieve_lexer_shift(scanner);

				if (sieve_lexer_curchar(scanner) != '\n') {
					sieve_lexer_error(lexer,
						"found stray carriage-return (CR) character "
						"in quoted string started at line %d",
						lexer->token_line);
					lexer->token_type = STT_ERROR;
					return FALSE;
				}

				if (str_len(str) <= SIEVE_MAX_STRING_LEN)
					str_append(str, "\r\n");
				break;

			/* Loose LF is allowed (non-standard) and converted to CRLF */
			case '\n':
				if (str_len(str) <= SIEVE_MAX_STRING_LEN)
					str_append(str, "\r\n");
				break;

			/* Other characters */
			default:
				if (str_len(str) <= SIEVE_MAX_STRING_LEN)
					str_append_c(str, sieve_lexer_curchar(scanner));
			}

			sieve_lexer_shift(scanner);
		}

		sieve_lexer_shift(scanner);

		if (str_len(str) > SIEVE_MAX_STRING_LEN) {
			sieve_lexer_error(lexer,
				"quoted string started at line %d is too long "
				"(longer than %llu bytes)", lexer->token_line,
				(long long) SIEVE_MAX_STRING_LEN);
			lexer->token_type = STT_ERROR;
			return FALSE;
		}

		lexer->token_type = STT_STRING;
		return TRUE;

	/* single character tokens */
	case ']':
		sieve_lexer_shift(scanner);
		lexer->token_type = STT_RSQUARE;
		return TRUE;
	case '[':
		sieve_lexer_shift(scanner);
		lexer->token_type = STT_LSQUARE;
		return TRUE;
	case '}':
		sieve_lexer_shift(scanner);
		lexer->token_type = STT_RCURLY;
		return TRUE;
	case '{':
		sieve_lexer_shift(scanner);
		lexer->token_type = STT_LCURLY;
		return TRUE;
	case ')':
		sieve_lexer_shift(scanner);
		lexer->token_type = STT_RBRACKET;
		return TRUE;
	case '(':
		sieve_lexer_shift(scanner);
		lexer->token_type = STT_LBRACKET;
		return TRUE;
	case ';':
		sieve_lexer_shift(scanner);
		lexer->token_type = STT_SEMICOLON;
		return TRUE;
	case ',':
		sieve_lexer_shift(scanner);
		lexer->token_type = STT_COMMA;
		return TRUE;

	/* EOF */
	case -1:
		if (!scanner->input->eof) {
			lexer->token_type = STT_ERROR;
			return FALSE;
		}
		lexer->token_type = STT_EOF;
		return TRUE;

	default:
		/* number */
		if (i_isdigit(sieve_lexer_curchar(scanner))) {
			return sieve_lexer_scan_number(scanner);

		/* identifier / tag */
		} else if (i_isalpha(sieve_lexer_curchar(scanner)) ||
			   sieve_lexer_curchar(scanner) == '_' ||
			   sieve_lexer_curchar(scanner) == ':') {

			enum sieve_token_type type = STT_IDENTIFIER;
			str_truncate(lexer->token_str_value,0);
			str = lexer->token_str_value;

			/* If it starts with a ':' it is a tag and not an
			   identifier */
 			if (sieve_lexer_curchar(scanner) == ':') {
				sieve_lexer_shift(scanner); // discard colon
				type = STT_TAG;

				/* First character still can't be a DIGIT */
 				if (i_isalpha(sieve_lexer_curchar(scanner)) ||
				    sieve_lexer_curchar(scanner) == '_') {
					str_append_c(str, sieve_lexer_curchar(scanner));
					sieve_lexer_shift(scanner);
				} else {
					/* Hmm, otherwise it is just a spurious
					   colon */
					lexer->token_type = STT_COLON;
					return TRUE;
				}
			} else {
				str_append_c(str, sieve_lexer_curchar(scanner));
				sieve_lexer_shift(scanner);
			}

			/* Scan the rest of the identifier */
			while (i_isalnum(sieve_lexer_curchar(scanner)) ||
			       sieve_lexer_curchar(scanner) == '_') {

				if (str_len(str) <= SIEVE_MAX_IDENTIFIER_LEN) {
	 				str_append_c(str, sieve_lexer_curchar(scanner));
				}
				sieve_lexer_shift(scanner);
			}

			/* Is this in fact a multiline text string ? */
			if (sieve_lexer_curchar(scanner) == ':' &&
			    type == STT_IDENTIFIER &&
			    str_len(str) == 4 &&
			    str_begins_icase_with(str_c(str), "text")) {
				sieve_lexer_shift(scanner); // discard colon

				/* Discard SP and HTAB whitespace */
				while (sieve_lexer_curchar(scanner) == ' ' ||
				       sieve_lexer_curchar(scanner) == '\t')
 					sieve_lexer_shift(scanner);

				/* Discard hash comment or handle single CRLF */
				if (sieve_lexer_curchar(scanner) == '\r')
					sieve_lexer_shift(scanner);
 				switch (sieve_lexer_curchar(scanner)) {
				case '#':
					if (!sieve_lexer_scan_hash_comment(scanner))
						return FALSE;
					if (scanner->input->eof) {
						sieve_lexer_error(lexer,
							"end of file before end of multi-line string");
						lexer->token_type = STT_ERROR;
						return FALSE;
					} else if (scanner->input->stream_errno != 0) {
						lexer->token_type = STT_ERROR;
						return FALSE;
					}
					break;
				case '\n':
					sieve_lexer_shift(scanner);
					break;
				case -1:
					if (scanner->input->eof) {
						sieve_lexer_error(lexer,
							"end of file before end of multi-line string");
					}
					lexer->token_type = STT_ERROR;
					return FALSE;
				default:
 					sieve_lexer_error(lexer,
 						"invalid character %s after 'text:' in multiline string",
						_char_sanitize(sieve_lexer_curchar(scanner)));
					lexer->token_type = STT_ERROR;
					return FALSE;
				}

				/* Start over */
				str_truncate(str, 0);

 				/* Parse literal lines */
				while (TRUE) {
					bool cr_shifted = FALSE;

					/* Remove dot-stuffing or detect end of text */
					if (sieve_lexer_curchar(scanner) == '.') {
						sieve_lexer_shift(scanner);

						/* Check for CR.. */
						if (sieve_lexer_curchar(scanner) == '\r') {
							sieve_lexer_shift(scanner);
							cr_shifted = TRUE;
						}

						/* ..LF */
						if (sieve_lexer_curchar(scanner) == '\n') {
							sieve_lexer_shift(scanner);

							/* End of multi-line string */

							/* Check whether length limit was violated */
							if (str_len(str) > SIEVE_MAX_STRING_LEN) {
								sieve_lexer_error(lexer,
									"multi-line string started at line %d is too long "
									"(longer than %llu bytes)", lexer->token_line,
									(long long) SIEVE_MAX_STRING_LEN);
									lexer->token_type = STT_ERROR;
									return FALSE;
							}

							lexer->token_type = STT_STRING;
							return TRUE;
						} else if (cr_shifted) {
							/* Seen CR, but no LF */
							if (sieve_lexer_curchar(scanner) != -1 ||
							    !scanner->input->eof) {
								sieve_lexer_error(lexer,
									"found stray carriage-return (CR) character "
									"in multi-line string started at line %d",
									lexer->token_line);
							}
							lexer->token_type = STT_ERROR;
							return FALSE;
						}

						/* Handle dot-stuffing */
						if (str_len(str) <= SIEVE_MAX_STRING_LEN)
							str_append_c(str, '.');
						if (sieve_lexer_curchar(scanner) == '.')
							sieve_lexer_shift(scanner);
					}

					/* Scan the rest of the line */
					while (sieve_lexer_curchar(scanner) != '\n' &&
					       sieve_lexer_curchar(scanner) != '\r') {

						switch (sieve_lexer_curchar(scanner)) {
						case -1:
							if (scanner->input->eof) {
								sieve_lexer_error(lexer,
									"end of file before end of multi-line string");
							}
 							lexer->token_type = STT_ERROR;
 							return FALSE;
						case '\0':
							sieve_lexer_error(lexer,
								"encountered NUL character in quoted string "
								"started at line %d", lexer->token_line);
							lexer->token_type = STT_ERROR;
							return FALSE;
						default:
							if (str_len(str) <= SIEVE_MAX_STRING_LEN)
  								str_append_c(str, sieve_lexer_curchar(scanner));
						}

						sieve_lexer_shift(scanner);
					}

					/* If exited loop due to CR, skip it */
					if (sieve_lexer_curchar(scanner) == '\r')
						sieve_lexer_shift(scanner);

					/* Now we must see an LF */
					if (sieve_lexer_curchar(scanner) != '\n') {
						if (sieve_lexer_curchar(scanner) != -1 ||
						    !scanner->input->eof) {
							sieve_lexer_error(lexer,
								"found stray carriage-return (CR) character "
								"in multi-line string started at line %d",
								lexer->token_line);
						}
 						lexer->token_type = STT_ERROR;
 						return FALSE;
					}

					if (str_len(str) <= SIEVE_MAX_STRING_LEN)
						str_append(str, "\r\n");

					sieve_lexer_shift(scanner);
				}

 				i_unreached();
			}

			if (str_len(str) > SIEVE_MAX_IDENTIFIER_LEN) {
				sieve_lexer_error(lexer,
					"encountered impossibly long %s%s'",
					(type == STT_TAG ? "tag identifier ':" :
					 "identifier '"),
					str_sanitize(str_c(str),
						     SIEVE_MAX_IDENTIFIER_LEN));
				lexer->token_type = STT_ERROR;
				return FALSE;
			}

			lexer->token_type = type;
			return TRUE;
		}

		/* Error (unknown character and EOF handled already) */
		if (lexer->token_type != STT_GARBAGE) {
			sieve_lexer_error(lexer,
				"unexpected character(s) starting with %s",
				_char_sanitize(sieve_lexer_curchar(scanner)));
		}
		sieve_lexer_shift(scanner);
		lexer->token_type = STT_GARBAGE;
		return FALSE;
	}
}

void sieve_lexer_skip_token(const struct sieve_lexer *lexer)
{
	/* Scan token while skipping whitespace */
	do {
		struct sieve_lexical_scanner *scanner = lexer->scanner;

		if (!sieve_lexer_scan_raw_token(scanner)) {
			if (!scanner->input->eof &&
			    scanner->input->stream_errno != 0) {
				sieve_critical(scanner->svinst, scanner->ehandler,
					sieve_error_script_location(scanner->script,
								    scanner->current_line),
					"error reading script",
					"error reading script during lexical analysis: %s",
					i_stream_get_error(scanner->input));
			}
			return;
		}
	} while (lexer->token_type == STT_WHITESPACE);
}

