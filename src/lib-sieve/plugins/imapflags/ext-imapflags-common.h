#ifndef __EXT_IMAPFLAGS_COMMON_H
#define __EXT_IMAPFLAGS_COMMON_H

#include "lib.h"

extern int ext_imapflags_my_id;
extern const struct sieve_extension imapflags_extension;

enum ext_imapflags_opcode {
	EXT_IMAPFLAGS_OPCODE_SETFLAG,
	EXT_IMAPFLAGS_OPCODE_ADDFLAG,
	EXT_IMAPFLAGS_OPCODE_REMOVEFLAG,
	EXT_IMAPFLAGS_OPCODE_HASFLAG
};

bool ext_imapflags_command_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);

bool ext_imapflags_command_opcode_dump
(const struct sieve_opcode *opcode,	
	const struct sieve_dumptime_env *denv, sieve_size_t *address);

/* Flag registration */

struct ext_imapflags_iter {
	string_t *flags_list;
	unsigned int offset;
	unsigned int last;
};

void ext_imapflags_iter_init
	(struct ext_imapflags_iter *iter, string_t *flags_list);
	
const char *ext_imapflags_iter_get_flag
	(struct ext_imapflags_iter *iter);

const char *ext_imapflags_get_flags_string
	(const struct sieve_runtime_env *renv);

void ext_imapflags_set_flags
	(const struct sieve_runtime_env *renv, string_t *flags);
void ext_imapflags_add_flags
	(const struct sieve_runtime_env *renv, string_t *flags);
void ext_imapflags_remove_flags
	(const struct sieve_runtime_env *renv, string_t *flags);

void ext_imapflags_get_flags_init
	(struct ext_imapflags_iter *iter, const struct sieve_runtime_env *renv);


#endif /* __EXT_IMAPFLAGS_COMMON_H */

