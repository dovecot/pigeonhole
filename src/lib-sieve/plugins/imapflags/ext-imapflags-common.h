#ifndef __EXT_IMAPFLAGS_COMMON_H
#define __EXT_IMAPFLAGS_COMMON_H

#include "lib.h"

#include "sieve-common.h"
#include "sieve-ext-variables.h"

extern int ext_imapflags_my_id;
extern const struct sieve_extension imapflags_extension;

enum ext_imapflags_opcode {
	EXT_IMAPFLAGS_OPERATION_SETFLAG,
	EXT_IMAPFLAGS_OPERATION_ADDFLAG,
	EXT_IMAPFLAGS_OPERATION_REMOVEFLAG,
	EXT_IMAPFLAGS_OPERATION_HASFLAG
};

bool ext_imapflags_command_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);

bool ext_imapflags_command_operands_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address);
bool ext_imapflags_command_operation_dump
(const struct sieve_operation *op,	
	const struct sieve_dumptime_env *denv, sieve_size_t *address);
	
bool ext_imapflags_command_operands_read
(	const struct sieve_runtime_env *renv, sieve_size_t *address,
	struct sieve_coded_stringlist **flag_list, 
	struct sieve_variable_storage **storage, unsigned int *var_index);
	
void ext_imapflags_attach_flags_tag
	(struct sieve_validator *valdtr, const char *command);

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
(const struct sieve_runtime_env *renv, struct sieve_variable_storage *storage, 
	unsigned int var_index);

void ext_imapflags_set_flags
	(const struct sieve_runtime_env *renv, struct sieve_variable_storage *storage,
		unsigned int var_index, string_t *flags);
void ext_imapflags_add_flags
	(const struct sieve_runtime_env *renv, struct sieve_variable_storage *storage,
		unsigned int var_index, string_t *flags);
void ext_imapflags_remove_flags
	(const struct sieve_runtime_env *renv, struct sieve_variable_storage *storage,
		unsigned int var_index, string_t *flags);

void ext_imapflags_get_flags_init
	(struct ext_imapflags_iter *iter, const struct sieve_runtime_env *renv,
		struct sieve_variable_storage *storage, unsigned int var_index);


#endif /* __EXT_IMAPFLAGS_COMMON_H */

