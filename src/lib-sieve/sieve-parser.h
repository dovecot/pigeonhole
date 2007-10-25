#ifndef __SIEVE_PARSER_H__
#define __SIEVE_PARSER_H__

#include "lib.h"
#include "sieve-lexer.h"
#include "sieve-ast.h"

struct sieve_parser;

struct sieve_parser *sieve_parser_create(int fd, struct sieve_ast *ast, struct sieve_error_handler *ehandler);
void sieve_parser_free(struct sieve_parser *parser);
bool sieve_parser_run(struct sieve_parser *parser);

#endif /* __SIEVE_PARSER_H__ */
