#ifndef __SIEVE_COMMON_H__
#define __SIEVE_COMMON_H__

/*
 * Predeclarations
 */

/* sieve-ast.h */
struct sieve_ast;
struct sieve_ast_node;
struct sieve_ast_argument;

/* sieve-commands.h */
struct sieve_tag;
struct sieve_command;
struct sieve_command_context;
struct sieve_command_registration;

/* sieve-code.h */
struct sieve_operation_extension;

/* sieve-lexer.h */
struct sieve_lexer;

/* sieve-parser.h */
struct sieve_parser;

/* sieve-validator.h */
struct sieve_validator;

/* sieve-generator.h */
struct sieve_jumplist;
struct sieve_generator;

/* sieve-interpreter.h */
struct sieve_interpreter;

/*
 *
 */
 
struct sieve_mail_context {
	struct mail_namespace *namespaces;
	struct mail_storage **storage_r;
	struct mail *mail;
	const char *destaddr;
	const char *mailbox;
};

#endif
