/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file 
 */

#ifndef __SIEVE_LEXER_H
#define __SIEVE_LEXER_H

#include "sieve-common.h"

enum sieve_token_type {
	STT_NONE,
	STT_WHITESPACE,
	STT_EOF,
  
	STT_NUMBER,
	STT_IDENTIFIER,
	STT_TAG,
	STT_STRING,
  
	STT_RBRACKET,
	STT_LBRACKET,
	STT_RCURLY,
	STT_LCURLY,
	STT_RSQUARE,
	STT_LSQUARE,
	STT_SEMICOLON,
	STT_COMMA,
  
	/* These are currently not used in the lexical specification, but a token
	 * is assigned to these to generate proper error messages (these are
	 * technically not garbage and possibly part of mistyped but otherwise
	 * valid tokens).
	 */
	STT_SLASH, 
	STT_COLON, 
  
	/* Error tokens */
	STT_GARBAGE, /* Error reporting deferred to parser */ 
	STT_ERROR    /* Lexer is responsible for error, parser won't report additional 
	                errors */
};

struct sieve_lexer;

/* Lexer object */
struct sieve_lexer *sieve_lexer_create
	(struct sieve_script *script, struct sieve_error_handler *ehandler);
void sieve_lexer_free(struct sieve_lexer **lexer);

/* Scanning */
bool sieve_lexer_skip_token(struct sieve_lexer *lexer);
const char *sieve_lexer_token_string(struct sieve_lexer *lexer);
void sieve_lexer_print_token(struct sieve_lexer *lexer);

/* Token access */
enum sieve_token_type sieve_lexer_current_token(struct sieve_lexer *lexer);
const string_t *sieve_lexer_token_str(struct sieve_lexer *lexer);
const char *sieve_lexer_token_ident(struct sieve_lexer *lexer);
int sieve_lexer_token_int(struct sieve_lexer *lexer);
int sieve_lexer_current_line(struct sieve_lexer *lexer);
bool sieve_lexer_eof(struct sieve_lexer *lexer);

#endif /* __SIEVE_LEXER_H */
