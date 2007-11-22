#ifndef __EXT_IMAPFLAGS_COMMON_H
#define __EXT_IMAPFLAGS_COMMON_H

extern int ext_imapflags_my_id;
extern const struct sieve_extension imapflags_extension;

enum ext_imapflags_opcode {
	EXT_IMAPFLAGS_OPCODE_SETFLAG,
	EXT_IMAPFLAGS_OPCODE_ADDFLAG,
	EXT_IMAPFLAGS_OPCODE_REMOVEFLAG
};

bool ext_imapflags_command_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);

#endif /* __EXT_IMAPFLAGS_COMMON_H */

