#ifndef __SIEVE_COMMON_H
#define __SIEVE_COMMON_H

#include <sys/types.h>
#include <stdint.h>

#include "sieve.h"

/* 
 * Types
 */
 
typedef uint64_t sieve_number_t;

/*
 * Predeclarations
 */

/* sieve-ast.h */
enum sieve_ast_argument_type;

struct sieve_ast;
struct sieve_ast_node;
struct sieve_ast_argument;

/* sieve-commands.h */
struct sieve_argument;
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

/* sieve-extension.h */
struct sieve_extension;

/* sieve-code.h */
struct sieve_operand;
struct sieve_opcode;
struct sieve_coded_stringlist;

/* sieve-binary.h */
typedef size_t sieve_size_t;
struct sieve_binary;

/* sieve-comparator.h */
struct sieve_comparator;

/* sieve-match-types.h */
struct sieve_match_type;
struct sieve_match_context;

/* sieve-address-parts.h */
struct sieve_address_part;

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

#endif /* __SIEVE_COMMON_H */
