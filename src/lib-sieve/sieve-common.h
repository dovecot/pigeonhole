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
struct sieve_runtime_env;
struct sieve_interpreter;

/* sieve-code-dumper.h */
struct sieve_dumptime_env;
struct sieve_code_dumper;

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

/* sieve-result.h */
struct sieve_result;
struct sieve_side_effects_list;

/* sieve-actions.h */
struct sieve_action_exec_env;
struct sieve_action;
struct sieve_side_effect;

#endif /* __SIEVE_COMMON_H */
