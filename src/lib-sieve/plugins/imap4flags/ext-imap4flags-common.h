/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#ifndef __EXT_IMAP4FLAGS_COMMON_H
#define __EXT_IMAP4FLAGS_COMMON_H

#include "lib.h"

#include "sieve-common.h"
#include "sieve-ext-variables.h"

#include "sieve-ext-imap4flags.h"

/*
 * Side effect
 */

extern const struct sieve_side_effect_def flags_side_effect;

/*
 * Operands
 */

extern const struct sieve_operand_def flags_side_effect_operand;

/*
 * Operations
 */

enum ext_imap4flags_opcode {
	EXT_IMAP4FLAGS_OPERATION_SETFLAG,
	EXT_IMAP4FLAGS_OPERATION_ADDFLAG,
	EXT_IMAP4FLAGS_OPERATION_REMOVEFLAG,
	EXT_IMAP4FLAGS_OPERATION_HASFLAG
};

extern const struct sieve_operation_def setflag_operation;
extern const struct sieve_operation_def addflag_operation;
extern const struct sieve_operation_def removeflag_operation;
extern const struct sieve_operation_def hasflag_operation;

/*
 * Commands
 */

extern const struct sieve_command_def cmd_setflag;
extern const struct sieve_command_def cmd_addflag;
extern const struct sieve_command_def cmd_removeflag;

extern const struct sieve_command_def tst_hasflag;

/*
 * Common command functions
 */

bool ext_imap4flags_command_validate
	(struct sieve_validator *valdtr, struct sieve_command *cmd);

/*
 * Flags tagged argument
 */

void ext_imap4flags_attach_flags_tag
	(struct sieve_validator *valdtr, const struct sieve_extension *ext,
		const char *command);

/*
 * Flag management
 */

struct ext_imap4flags_iter {
	string_t *flags_list;
	unsigned int offset;
	unsigned int last;
};

void ext_imap4flags_iter_init
	(struct ext_imap4flags_iter *iter, string_t *flags_list);

const char *ext_imap4flags_iter_get_flag
	(struct ext_imap4flags_iter *iter);

/* Flag operations */

typedef int (*ext_imapflag_flag_operation_t)
	(const struct sieve_runtime_env *renv,
		const struct sieve_extension *flg_ext,
		struct sieve_variable_storage *storage,
		unsigned int var_index, struct sieve_stringlist *flags)
		ATTR_NULL(2);

/* Flags access */

void ext_imap4flags_get_implicit_flags_init
	(struct ext_imap4flags_iter *iter, const struct sieve_extension *this_ext,
		struct sieve_result *result);


#endif /* __EXT_IMAP4FLAGS_COMMON_H */

