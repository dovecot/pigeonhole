/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file 
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
#include <stdlib.h>
#include <unistd.h>

/* 
 * Useful macros
 */

#define IS_DIGIT(c) ( c >= '0' && c <= '9' )
#define DIGIT_VAL(c) ( c - '0' )
#define IS_ALPHA(c) ( (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') )

/*
 * Forward declarations
 */
 
inline static void sieve_lexer_error
	(struct sieve_lexer *lexer, const char *fmt, ...) ATTR_FORMAT(2, 3);
inline static void sieve_lexer_warning
	(struct sieve_lexer *lexer, const char *fmt, ...) ATTR_FORMAT(2, 3);

/*
 * Lexer object
 */

struct sieve_lexer {
	pool_t pool;

	struct sieve_script *script;
	struct istream *input;
		
	int current_line;
	
	enum sieve_token_type token_type;
	string_t *token_str_value;
	int token_int_value;
	
	struct sieve_error_handler *ehandler;
	
	/* Currently scanned data */
	const unsigned char *buffer;
	size_t buffer_size;
	size_t buffer_pos;
};

struct sieve_lexer *sieve_lexer_create
(struct sieve_script *script, struct sieve_error_handler *ehandler) 
{
	pool_t pool;
	struct sieve_lexer *lexer;
	struct istream *stream;
	
	stream = sieve_script_open(script, NULL);
	if ( stream == NULL )
		return NULL;
	
	pool = pool_alloconly_create("sieve_lexer", 1024);	
	lexer = p_new(pool, struct sieve_lexer, 1);
	lexer->pool = pool;
	
	lexer->ehandler = ehandler;
	sieve_error_handler_ref(ehandler);

	lexer->input = stream;
	i_stream_ref(lexer->input);
	
	lexer->script = script;
	sieve_script_ref(script);
	
	lexer->buffer = NULL;
	lexer->buffer_size = 0;
	lexer->buffer_pos = 0;
	
	lexer->current_line = 1;	
	lexer->token_type = STT_NONE;
	lexer->token_str_value = str_new(pool, 256);
	lexer->token_int_value = 0;
		
	return lexer;
}

void sieve_lexer_free(struct sieve_lexer **lexer) 
{	
	i_stream_unref(&(*lexer)->input);

	sieve_script_close((*lexer)->script);
	sieve_script_unref(&(*lexer)->script);

	sieve_error_handler_unref(&(*lexer)->ehandler);

	pool_unref(&(*lexer)->pool); 

	*lexer = NULL;
}

/*
 * Internal error handling
 */

inline static void sieve_lexer_error
(struct sieve_lexer *lexer, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	T_BEGIN {
		sieve_verror(lexer->ehandler, 
			sieve_error_script_location(lexer->script, lexer->current_line),
			fmt, args);
	} T_END;
		
	va_end(args);
}

inline static void sieve_lexer_warning
(struct sieve_lexer *lexer, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	T_BEGIN { 
		sieve_vwarning(lexer->ehandler, 
			sieve_error_script_location(lexer->script, lexer->current_line),
			fmt, args);
	} T_END;
		
	va_end(args);
}

const char *sieve_lexer_token_string(struct sieve_lexer *lexer) 
{
	switch ( lexer->token_type ) {
		case STT_NONE: return "no token (bug)"; 		
		case STT_WHITESPACE: return "whitespace (bug)";
		case STT_EOF: return "end of file";
  
		case STT_NUMBER: return "number"; 
		case STT_IDENTIFIER: return "identifier"; 
		case STT_TAG: return "tag";
		case STT_STRING: return "string"; 
  
		case STT_RBRACKET: return "')'"; 
		case STT_LBRACKET: return "'('";
		case STT_RCURLY: return "'}'"; 
		case STT_LCURLY: return "'{'"; 
		case STT_RSQUARE: return "']'"; 
		case STT_LSQUARE: return "'['"; 
		case STT_SEMICOLON: return "';'"; 
		case STT_COMMA: return "','"; 
  	
		case STT_SLASH: return "'/'";  
		case STT_COLON: return "':'";   
  
		case STT_GARBAGE: return "unknown characters"; 
		case STT_ERROR: return "error token (bug)";
	}
   
	return "unknown token (bug)";
}
	
/* 
 * Debug 
 */
 
void sieve_lexer_print_token(struct sieve_lexer *lexer) 
{
	switch ( lexer->token_type ) {
		case STT_NONE: printf("??NONE?? "); break;		
		case STT_WHITESPACE: printf("??WHITESPACE?? "); break;
		case STT_EOF: printf("EOF\n"); break;
  
		case STT_NUMBER: printf("NUMBER "); break;
		case STT_IDENTIFIER: printf("IDENTIFIER "); break;
		case STT_TAG: printf("TAG "); break;
		case STT_STRING: printf("STRING "); break;
  
		case STT_RBRACKET: printf(") "); break;
		case STT_LBRACKET: printf("( "); break;
		case STT_RCURLY: printf("}\n"); break;
		case STT_LCURLY: printf("{\n"); break;
		case STT_RSQUARE: printf("] "); break;
		case STT_LSQUARE: printf("[ "); break;
		case STT_SEMICOLON: printf(";\n"); break;
		case STT_COMMA: printf(", "); break;
  
		case STT_SLASH: printf("/ "); break; 
		case STT_COLON: printf(": "); break;  
  	
		case STT_GARBAGE: printf(">>GARBAGE<<"); break;
		case STT_ERROR: printf(">>ERROR<<"); break;
	default: 
		printf("UNKNOWN ");
		break;
	}
}

/*
 * Token access
 */ 

enum sieve_token_type sieve_lexer_current_token(struct sieve_lexer *lexer) 
{
	return lexer->token_type;
}

const string_t *sieve_lexer_token_str(struct sieve_lexer *lexer) 
{
	i_assert(	lexer->token_type == STT_STRING );
		
	return lexer->token_str_value;
}

const char *sieve_lexer_token_ident(struct sieve_lexer *lexer) 
{
	i_assert(
		lexer->token_type == STT_TAG ||
		lexer->token_type == STT_IDENTIFIER);
		
	return str_c(lexer->token_str_value);
}

int sieve_lexer_token_int(struct sieve_lexer *lexer) 
{
	i_assert(lexer->token_type == STT_NUMBER);
		
	return lexer->token_int_value;
}

bool sieve_lexer_eof(struct sieve_lexer *lexer) 
{
	return lexer->token_type == STT_EOF;
}

int sieve_lexer_current_line(struct sieve_lexer *lexer) 
{
	return lexer->current_line;
}

/*
 * Lexical scanning 
 */

static void sieve_lexer_shift(struct sieve_lexer *lexer) 
{
	if ( lexer->buffer != NULL && lexer->buffer[lexer->buffer_pos] == '\n' ) 
		lexer->current_line++;	
	
	if ( lexer->buffer != NULL && lexer->buffer_pos + 1 < lexer->buffer_size )
		lexer->buffer_pos++;
	else {
		if ( lexer->buffer != NULL )
			i_stream_skip(lexer->input, lexer->buffer_size);
		
		lexer->buffer = i_stream_get_data(lexer->input, &lexer->buffer_size);
	  
		if ( lexer->buffer == NULL && i_stream_read(lexer->input) > 0 )
	  		lexer->buffer = i_stream_get_data(lexer->input, &lexer->buffer_size);
	  	
		lexer->buffer_pos = 0;
	}
}

static inline int sieve_lexer_curchar(struct sieve_lexer *lexer) 
{	
	if ( lexer->buffer == NULL )
		return -1;
	
	return lexer->buffer[lexer->buffer_pos];
}

/* sieve_lexer_scan_raw_token:
 *   Scans valid tokens and whitespace 
 */
static bool sieve_lexer_scan_raw_token(struct sieve_lexer *lexer) 
{
	sieve_number_t start_line;
	string_t *str;

	/* Read first character */
	if ( lexer->token_type == STT_NONE ) {
		i_stream_read(lexer->input);
		sieve_lexer_shift(lexer);
	}
  
	switch ( sieve_lexer_curchar(lexer) ) {
	
	/* whitespace */
	
	// hash-comment = ( "#" *CHAR-NOT-CRLF CRLF )
	case '#': 
		sieve_lexer_shift(lexer);
		while ( sieve_lexer_curchar(lexer) != '\n' ) {
			if ( sieve_lexer_curchar(lexer) == -1 ) {
			  sieve_lexer_error(lexer, "end of file before end of hash comment");
			  lexer->token_type = STT_ERROR;
				return FALSE;
			}
			sieve_lexer_shift(lexer);
		} 
		sieve_lexer_shift(lexer);
		
		lexer->token_type = STT_WHITESPACE;
		return TRUE;
		
	// bracket-comment = "/*" *(CHAR-NOT-STAR / ("*" CHAR-NOT-SLASH)) "*/"
	//        ;; No */ allowed inside a comment.
	//        ;; (No * is allowed unless it is the last character,
	//        ;; or unless it is followed by a character that isn't a
	//        ;; slash.)
	case '/': 
		start_line = lexer->current_line;
		sieve_lexer_shift(lexer);
		
		if ( sieve_lexer_curchar(lexer) == '*' ) { 
			sieve_lexer_shift(lexer);
			
			while ( TRUE ) {
				if ( sieve_lexer_curchar(lexer) == '*' ) {
					sieve_lexer_shift(lexer);
					
					if ( sieve_lexer_curchar(lexer) == '/' ) {
						sieve_lexer_shift(lexer);
						
						lexer->token_type = STT_WHITESPACE;
						return TRUE;
						
					} else if ( sieve_lexer_curchar(lexer) == -1 ) {
						sieve_lexer_error(lexer, 
							"end of file before end of bracket comment ('/* ... */') "
							"started at line %d", start_line);
						lexer->token_type = STT_ERROR;
						return FALSE;
					}

				} else if ( sieve_lexer_curchar(lexer) == -1 ) {
					sieve_lexer_error(lexer, 
						"end of file before end of bracket comment ('/* ... */') "
						"started at line %d", start_line);
					lexer->token_type = STT_ERROR;
					return FALSE;
					
				} else 
					sieve_lexer_shift(lexer);					
			}
			
			i_unreached();
			return FALSE;
		}
		
		lexer->token_type = STT_SLASH;
		return TRUE;
		
	// comment = bracket-comment / hash-comment
  	// white-space = 1*(SP / CRLF / HTAB) / comment
	case '\t':
	case '\r':
	case '\n':
	case ' ':
		sieve_lexer_shift(lexer);
		
		while ( sieve_lexer_curchar(lexer) == '\t' ||
			sieve_lexer_curchar(lexer) == '\r' ||
			sieve_lexer_curchar(lexer) == '\n' ||
			sieve_lexer_curchar(lexer) == ' ' ) {
			
			sieve_lexer_shift(lexer);
		}
		
		lexer->token_type = STT_WHITESPACE;
		return TRUE;
		
	/* quoted-string */
	case '"':
		start_line = lexer->current_line;
		sieve_lexer_shift(lexer);
		str_truncate(lexer->token_str_value, 0);
		str = lexer->token_str_value;
		
		while ( sieve_lexer_curchar(lexer) != '"' ) {
			if ( sieve_lexer_curchar(lexer) == -1 ) {
				sieve_lexer_error(lexer, 
					"end of file before end of quoted string "
					"started at line %d", start_line);
				lexer->token_type = STT_ERROR;
				return FALSE;
			}
			
			if ( sieve_lexer_curchar(lexer) == '\\' ) {
				sieve_lexer_shift(lexer);
			}

			if ( str_len(str) <= SIEVE_MAX_STRING_LEN ) 
				str_append_c(str, sieve_lexer_curchar(lexer));

			sieve_lexer_shift(lexer);
		}

		sieve_lexer_shift(lexer);

		if ( str_len(str) > SIEVE_MAX_STRING_LEN ) {
			sieve_lexer_error(lexer, 
				"quoted string started at line %d is too long "
				"(longer than %llu bytes)", start_line,
				(long long) SIEVE_MAX_STRING_LEN);
			lexer->token_type = STT_ERROR;
			return FALSE;
		}
		
		lexer->token_type = STT_STRING;
		return TRUE;
		
	/* single character tokens */
	case ']':
		sieve_lexer_shift(lexer);
		lexer->token_type = STT_RSQUARE;
		return TRUE;
	case '[':
		sieve_lexer_shift(lexer);
		lexer->token_type = STT_LSQUARE;
		return TRUE;
	case '}':
		sieve_lexer_shift(lexer);
		lexer->token_type = STT_RCURLY;
		return TRUE;
	case '{':
		sieve_lexer_shift(lexer);
		lexer->token_type = STT_LCURLY;
		return TRUE;
	case ')':
		sieve_lexer_shift(lexer);
		lexer->token_type = STT_RBRACKET;
		return TRUE;
	case '(':
		sieve_lexer_shift(lexer);
		lexer->token_type = STT_LBRACKET;	
		return TRUE;
	case ';':
		sieve_lexer_shift(lexer);
		lexer->token_type = STT_SEMICOLON;
		return TRUE;
	case ',':
		sieve_lexer_shift(lexer);
		lexer->token_type = STT_COMMA;
		return TRUE;
		
	/* EOF */	
	case -1: 
	  lexer->token_type = STT_EOF;
		return TRUE;
		
	default: 
		/* number */
		if ( IS_DIGIT(sieve_lexer_curchar(lexer)) ) {
			sieve_number_t value = DIGIT_VAL(sieve_lexer_curchar(lexer));
			bool overflow = FALSE;

			sieve_lexer_shift(lexer);
  		
			while ( IS_DIGIT(sieve_lexer_curchar(lexer)) ) {
				sieve_number_t valnew = 
					value * 10 + DIGIT_VAL(sieve_lexer_curchar(lexer));
			
				/* Check for integer wrap */
				if ( valnew < value )
					overflow = TRUE;

				value = valnew;
				sieve_lexer_shift(lexer);
 			}
  		
			switch ( sieve_lexer_curchar(lexer) ) { 
			case 'k':
			case 'K': /* Kilo */
				if ( value > (SIEVE_MAX_NUMBER >> 10) )
					overflow = TRUE;
				else
					value = value << 10;
				sieve_lexer_shift(lexer);
				break;
			case 'm': 
			case 'M': /* Mega */
				if ( value > (SIEVE_MAX_NUMBER >> 20) )
					overflow = TRUE;
				else
					value = value << 20;
				sieve_lexer_shift(lexer);
				break;
			case 'g':
			case 'G': /* Giga */
				if ( value > (SIEVE_MAX_NUMBER >> 30) )
					overflow = TRUE;
				else
					value = value << 30;
				sieve_lexer_shift(lexer);
				break;
			default:
				/* Next token */
				break;
			}

			/* Check for integer wrap */
			if ( overflow ) {
				sieve_lexer_error(lexer,
					"number exceeds integer limits (max %llu)",
					(long long) SIEVE_MAX_NUMBER);
				lexer->token_type = STT_ERROR;
				return FALSE;
			}
  	
			lexer->token_type = STT_NUMBER;
			lexer->token_int_value = value;
			return TRUE;	
  		
		/* identifier / tag */	
		} else if ( IS_ALPHA(sieve_lexer_curchar(lexer)) ||
			sieve_lexer_curchar(lexer) == '_' || 
			sieve_lexer_curchar(lexer) == ':' ) {
  		
			enum sieve_token_type type = STT_IDENTIFIER;
			str_truncate(lexer->token_str_value,0);
			str = lexer->token_str_value;
  		
			/* If it starts with a ':' it is a tag and not an identifier */
 			if ( sieve_lexer_curchar(lexer) == ':' ) {
				sieve_lexer_shift(lexer); // discard colon
				type = STT_TAG;
  			
				/* First character still can't be a DIGIT */
 				if ( IS_ALPHA(sieve_lexer_curchar(lexer)) ||
					sieve_lexer_curchar(lexer) == '_' ) { 
					str_append_c(str, sieve_lexer_curchar(lexer));
					sieve_lexer_shift(lexer);
				} else {
					/* Hmm, otherwise it is just a spurious colon */
					lexer->token_type = STT_COLON;
					return TRUE;
				}
			} else {
				str_append_c(str, sieve_lexer_curchar(lexer));
				sieve_lexer_shift(lexer);
			}
  		
			/* Scan the rest of the identifier */
			while ( IS_ALPHA(sieve_lexer_curchar(lexer)) ||
				IS_DIGIT(sieve_lexer_curchar(lexer)) ||
				sieve_lexer_curchar(lexer) == '_' ) {

				if ( str_len(str) <= SIEVE_MAX_IDENTIFIER_LEN ) {
	 				str_append_c(str, sieve_lexer_curchar(lexer));
				}
				sieve_lexer_shift(lexer);
			}

			/* Is this in fact a multiline text string ? */
			if ( sieve_lexer_curchar(lexer) == ':' &&
				type == STT_IDENTIFIER && str_len(str) == 4 &&
				strncasecmp(str_c(str), "text", 4) == 0 ) {
				sieve_lexer_shift(lexer); // discard colon

				start_line = lexer->current_line;
  			
				/* Discard SP and HTAB whitespace */
				while ( sieve_lexer_curchar(lexer) == ' ' || 
					sieve_lexer_curchar(lexer) == '\t' )
 					sieve_lexer_shift(lexer);
  				
				/* Discard hash comment or handle single CRLF */
				if ( sieve_lexer_curchar(lexer) == '#' ) {
					while ( sieve_lexer_curchar(lexer) != '\n' )
						sieve_lexer_shift(lexer);
				} else if ( sieve_lexer_curchar(lexer) == '\r' ) {
					sieve_lexer_shift(lexer);
				}
  			
				/* Terminating LF required */
 				if ( sieve_lexer_curchar(lexer) == '\n' ) {
					sieve_lexer_shift(lexer);
				} else {
					if ( sieve_lexer_curchar(lexer) == -1 ) {
						sieve_lexer_error(lexer, 
							"end of file before end of multi-line string");
					} else {
 						sieve_lexer_error(lexer, 
 							"invalid character '%c' after 'text:' in multiline string",
							sieve_lexer_curchar(lexer));
					}

					lexer->token_type = STT_ERROR;
					return FALSE;
				}
  			
				/* Start over */
				str_truncate(str, 0); 
  			
 				/* Parse literal lines */
				while ( TRUE ) {
					bool cr_shifted = FALSE;

					/* Remove dot-stuffing or detect end of text */
					if ( sieve_lexer_curchar(lexer) == '.' ) {
						sieve_lexer_shift(lexer);
  					
						/* Check for CRLF */
						if ( sieve_lexer_curchar(lexer) == '\r' ) {
							sieve_lexer_shift(lexer);
							cr_shifted = TRUE;
						}
  				
						if ( sieve_lexer_curchar(lexer) == '\n' ) {
							sieve_lexer_shift(lexer);

							if ( str_len(str) > SIEVE_MAX_STRING_LEN ) {
								sieve_lexer_error(lexer, 
									"literal string started at line %d is too long "
									"(longer than %llu bytes)", start_line,
									(long long) SIEVE_MAX_STRING_LEN);
									lexer->token_type = STT_ERROR;
									return FALSE;
							}

							lexer->token_type = STT_STRING;
							return TRUE;
						} else if ( cr_shifted ) {
							sieve_lexer_error(lexer,
                                "found CR without subsequent LF in multi-line string literal");
                            lexer->token_type = STT_ERROR;
                            return FALSE;
						}

						/* Handle dot-stuffing */
						if ( str_len(str) <= SIEVE_MAX_STRING_LEN ) 
							str_append_c(str, '.');
						if ( sieve_lexer_curchar(lexer) == '.' ) 
	                        sieve_lexer_shift(lexer);
					}
  				
					/* Scan the rest of the line */
					while ( sieve_lexer_curchar(lexer) != '\n' ) {
						if ( sieve_lexer_curchar(lexer) == -1 ) {
							sieve_lexer_error(lexer, 
								"end of file before end of multi-line string");
 							lexer->token_type = STT_ERROR;
 							return FALSE;
 						}
						
						if ( str_len(str) <= SIEVE_MAX_STRING_LEN ) 
  							str_append_c(str, sieve_lexer_curchar(lexer));

						sieve_lexer_shift(lexer);
					}

					if ( str_len(str) <= SIEVE_MAX_STRING_LEN ) 
						str_append_c(str, '\n');

					sieve_lexer_shift(lexer);
				}
  			
 				i_unreached();
				lexer->token_type = STT_ERROR;
				return FALSE;
			}

			if ( str_len(str) > SIEVE_MAX_IDENTIFIER_LEN ) {
				sieve_lexer_error(lexer, 
					"encountered impossibly long %s%s'",
					(type == STT_TAG ? "tag identifier ':" : "identifier '"), 
					str_sanitize(str_c(str), SIEVE_MAX_IDENTIFIER_LEN));
				lexer->token_type = STT_ERROR;
				return FALSE;
			}
  			
			lexer->token_type = type;
			return TRUE;
		}
	
		/* Error (unknown character and EOF handled already) */
		if ( lexer->token_type != STT_GARBAGE ) 
			sieve_lexer_error(lexer, "unexpected character(s) starting with '%c'", 
				sieve_lexer_curchar(lexer));
		sieve_lexer_shift(lexer);
		lexer->token_type = STT_GARBAGE;
		return FALSE;
	}
}

bool sieve_lexer_skip_token(struct sieve_lexer *lexer) 
{
	/* Scan token */
	if ( !sieve_lexer_scan_raw_token(lexer) ) return FALSE;
	
	/* Skip any whitespace */	
	while ( lexer->token_type == STT_WHITESPACE ) {
		if ( !sieve_lexer_scan_raw_token(lexer) ) return FALSE;
	}
	
	return TRUE;
}

