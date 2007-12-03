#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "lib.h"
#include "str.h"
#include "istream.h"

#include "sieve-lexer.h"
#include "sieve-error.h"

#define IS_DIGIT(c) ( c >= '0' && c <= '9' )
#define DIGIT_VAL(c) ( c - '0' )
#define IS_ALPHA(c) ( (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') )
#define IS_QUANTIFIER(c) (c == 'K' || c == 'M' || c =='G') 

struct sieve_lexer {
	pool_t pool;
	const char *scriptname;
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

inline static void sieve_lexer_error
(struct sieve_lexer *lexer, const char *fmt, ...) 
{
	va_list args;
	va_start(args, fmt);

	sieve_verror(lexer->ehandler, 
		t_strdup_printf("%s:%d", lexer->scriptname, lexer->current_line),
		fmt, args);
		
	va_end(args);
}

inline static void sieve_lexer_warning
(struct sieve_lexer *lexer, const char *fmt, ...) 
{
	va_list args;
	va_start(args, fmt);

	sieve_vwarning(lexer->ehandler, 
		t_strdup_printf("%s:%d", lexer->scriptname, lexer->current_line),
		fmt, args);
		
	va_end(args);
}

struct sieve_lexer *sieve_lexer_create
(struct istream *stream, const char *scriptname, 
	struct sieve_error_handler *ehandler) 
{
	pool_t pool = pool_alloconly_create("sieve_lexer", 1024);	
	struct sieve_lexer *lexer = p_new(pool, struct sieve_lexer, 1);

	lexer->pool = pool;
	lexer->scriptname = p_strdup(pool, scriptname);
	lexer->input = stream;
	
	lexer->buffer = NULL;
	lexer->buffer_size = 0;
	lexer->buffer_pos = 0;
	
	lexer->current_line = 1;	
	lexer->token_type = STT_NONE;
	lexer->token_str_value = NULL;
	lexer->token_int_value = 0;
	
	lexer->ehandler = ehandler;
	
	return lexer;
}

void sieve_lexer_free(struct sieve_lexer *lexer ATTR_UNUSED) {
	pool_unref(&(lexer->pool)); /* This frees any allocated string value as well */
}

static void sieve_lexer_shift(struct sieve_lexer *lexer) {
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

	/*if ( lexer->buffer != NULL )	
		printf("D %c\n", lexer->buffer[lexer->buffer_pos]);
	else 
  	printf("NULL!\n");*/
}

static __inline__ int sieve_lexer_curchar(struct sieve_lexer *lexer) {	
	if ( lexer->buffer == NULL )
		return -1;
	
	return lexer->buffer[lexer->buffer_pos];
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

inline enum sieve_token_type sieve_lexer_current_token(struct sieve_lexer *lexer) {
	return lexer->token_type;
}

inline const string_t *sieve_lexer_token_str(struct sieve_lexer *lexer) {
	i_assert(	lexer->token_type == STT_STRING );
		
	return lexer->token_str_value;
}

inline const char *sieve_lexer_token_ident(struct sieve_lexer *lexer) {
	i_assert(
		lexer->token_type == STT_TAG ||
		lexer->token_type == STT_IDENTIFIER);
		
	return str_c(lexer->token_str_value);
}

inline int sieve_lexer_token_int(struct sieve_lexer *lexer) {
	i_assert(lexer->token_type == STT_NUMBER);
		
	return lexer->token_int_value;
}

inline bool sieve_lexer_eof(struct sieve_lexer *lexer) {
	return lexer->token_type == STT_EOF;
}

inline int sieve_lexer_current_line(struct sieve_lexer *lexer) {
	return lexer->current_line;
}

/* sieve_lexer_scan_raw_token:
 *   Scans valid tokens and whitespace 
 */
bool sieve_lexer_scan_raw_token(struct sieve_lexer *lexer) 
{
	int start_line;
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
		str = str_new(lexer->pool, 16);
		lexer->token_str_value = str;
		
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
		
			str_append_c(str, sieve_lexer_curchar(lexer));	
			sieve_lexer_shift(lexer);
		}
		
		sieve_lexer_shift(lexer);
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
			int value = DIGIT_VAL(sieve_lexer_curchar(lexer));
			sieve_lexer_shift(lexer);
  		
			while ( IS_DIGIT(sieve_lexer_curchar(lexer)) ) {
				value = value * 10 + DIGIT_VAL(sieve_lexer_curchar(lexer));
				sieve_lexer_shift(lexer);
 			}
  		
			switch ( sieve_lexer_curchar(lexer) ) { 
			case 'K': /* Kilo */
				value *= 1024;
				sieve_lexer_shift(lexer);
				break; 
			case 'M': /* Mega */
				value *= 1024*1024;
				sieve_lexer_shift(lexer);
				break;
			case 'G': /* Giga */
				value *= 1024*1024*1024;
				sieve_lexer_shift(lexer);
				break;
			default:
				/* Next token */
				break;
			}
  	
			lexer->token_type = STT_NUMBER;
			lexer->token_int_value = value;
			return TRUE;	
  		
		/* identifier / tag */	
		} else if ( IS_ALPHA(sieve_lexer_curchar(lexer)) ||
			sieve_lexer_curchar(lexer) == '_' || 
			sieve_lexer_curchar(lexer) == ':' ) {
  		
			enum sieve_token_type type = STT_IDENTIFIER;
			str = str_new(lexer->pool, 16);
			lexer->token_str_value = str;
  		
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
				IS_ALPHA(sieve_lexer_curchar(lexer)) ||
				sieve_lexer_curchar(lexer) == '_' ) {
 				str_append_c(str, sieve_lexer_curchar(lexer));
				sieve_lexer_shift(lexer);
			}
  			
			/* Is this in fact a multiline text string ? */
			if ( sieve_lexer_curchar(lexer) == ':' &&
				type == STT_IDENTIFIER && str_len(str) == 4 &&
				strncasecmp(str_c(str), "text", 4) == 0 ) {
				sieve_lexer_shift(lexer); // discard colon
  			
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
						sieve_lexer_error(lexer, "end of file before end of multi-line string");
					} else {
 						sieve_lexer_error(lexer, "invalid character '%c' after 'text:' in multiline string",
							sieve_lexer_curchar(lexer));
					}

					lexer->token_type = STT_ERROR;
					return FALSE;
				}
  			
				/* Start over */
				str_truncate(str, 0); 
  			
 				/* Parse literal lines */
				while ( TRUE ) {
					/* Remove dot-stuffing or detect end of text */
					if ( sieve_lexer_curchar(lexer) == '.' ) {
						bool cr_shifted = FALSE;
						sieve_lexer_shift(lexer);
  					
						/* Check for CRLF */
						if ( sieve_lexer_curchar(lexer) == '\r' ) 
							sieve_lexer_shift(lexer);
  				
						if ( sieve_lexer_curchar(lexer) == '\n' ) {
							sieve_lexer_shift(lexer);
							lexer->token_type = STT_STRING;
							return TRUE;
						} else if ( cr_shifted ) 
							str_append_c(str, '\r');  	
					}
  				
					/* Scan the rest of the line */
					while ( sieve_lexer_curchar(lexer) != '\n' ) {
						if ( sieve_lexer_curchar(lexer) == -1 ) {
							sieve_lexer_error(lexer, "end of file before end of multi-line string");
 							lexer->token_type = STT_ERROR;
 							return FALSE;
 						}
  					
						str_append_c(str, sieve_lexer_curchar(lexer));
						sieve_lexer_shift(lexer);
					}
					str_append_c(str, '\n');
					sieve_lexer_shift(lexer);
				}
  			
 				i_unreached();
				lexer->token_type = STT_ERROR;
				return FALSE;
			}
  			
			lexer->token_type = type;
			return TRUE;
		}
	
		/* Error (unknown character and EOF handled already) */
		if ( lexer->token_type != STT_GARBAGE ) 
			sieve_lexer_error( lexer, "unexpected character(s) starting with '%c'", sieve_lexer_curchar(lexer) );
		sieve_lexer_shift(lexer);
		lexer->token_type = STT_GARBAGE;
		return FALSE;
	}
}

bool sieve_lexer_skip_token(struct sieve_lexer *lexer) 
{
	/* Free any previously allocated string value */
	if ( lexer->token_str_value != NULL ) {
		str_free(&lexer->token_str_value);
		lexer->token_str_value = NULL;
	}
	
	/* Scan token */
	if ( !sieve_lexer_scan_raw_token(lexer) ) return FALSE;
	
	/* Skip any whitespace */	
	while ( lexer->token_type == STT_WHITESPACE ) {
		if ( !sieve_lexer_scan_raw_token(lexer) ) return FALSE;
	}
	
	//sieve_lexer_print_token(lexer);
	//printf("\n");
	
	return TRUE;
}

