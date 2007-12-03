#ifndef __SIEVE_PARSER_H
#define __SIEVE_PARSER_H

#include "lib.h"
#include "sieve-lexer.h"
#include "sieve-ast.h"

struct sieve_parser;

struct sieve_parser *sieve_parser_create
	(int fd, const char *scriptname, struct sieve_error_handler *ehandler);
void sieve_parser_free(struct sieve_parser *parser);
bool sieve_parser_run(struct sieve_parser *parser, struct sieve_ast **ast);

#endif /* __SIEVE_PARSER_H */
