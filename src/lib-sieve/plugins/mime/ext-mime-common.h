#ifndef EXT_FOREVERYPART_COMMON_H
#define EXT_FOREVERYPART_COMMON_H

#include "sieve-message.h"

/*
 * Extension
 */

struct ext_extracttext_context {
	const struct sieve_extension *var_ext;
	const struct sieve_extension *fep_ext;
};

extern const struct sieve_extension_def foreverypart_extension;
extern const struct sieve_extension_def mime_extension;
extern const struct sieve_extension_def extracttext_extension;

/*
 * Tagged arguments
 */

extern const struct sieve_argument_def mime_tag;
extern const struct sieve_argument_def mime_anychild_tag;
extern const struct sieve_argument_def mime_type_tag;
extern const struct sieve_argument_def mime_subtype_tag;
extern const struct sieve_argument_def mime_contenttype_tag;
extern const struct sieve_argument_def mime_param_tag;

/*
 * Commands
 */

struct ext_foreverypart_loop {
	const char *name;
	struct sieve_jumplist *exit_jumps;
};

extern const struct sieve_command_def cmd_foreverypart;
extern const struct sieve_command_def cmd_break;
extern const struct sieve_command_def cmd_extracttext;

/*
 * Operations
 */

extern const struct sieve_operation_def foreverypart_begin_operation;
extern const struct sieve_operation_def foreverypart_end_operation;
extern const struct sieve_operation_def break_operation;
extern const struct sieve_operation_def extracttext_operation;

enum ext_foreverypart_opcode {
	EXT_FOREVERYPART_OPERATION_FOREVERYPART_BEGIN,
	EXT_FOREVERYPART_OPERATION_FOREVERYPART_END,
	EXT_FOREVERYPART_OPERATION_BREAK,
};

/*
 * Operands
 */

enum ext_mime_option {
	EXT_MIME_OPTION_NONE = 0,
	EXT_MIME_OPTION_TYPE,
	EXT_MIME_OPTION_SUBTYPE,
	EXT_MIME_OPTION_CONTENTTYPE,
	EXT_MIME_OPTION_PARAM
};

extern const struct sieve_operand_def mime_operand;

/*
 * Foreverypart loop
 */

struct ext_foreverypart_runtime_loop {
	struct sieve_message_part_iter part_iter;
	struct sieve_message_part *part;
};

struct ext_foreverypart_runtime_loop *
ext_foreverypart_runtime_loop_get_current
(const struct sieve_runtime_env *renv);

#endif
