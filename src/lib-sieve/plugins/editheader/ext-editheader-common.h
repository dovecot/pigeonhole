#ifndef EXT_EDITHEADER_COMMON_H
#define EXT_EDITHEADER_COMMON_H

/*
 * Commands
 */

extern const struct sieve_command_def addheader_command;
extern const struct sieve_command_def deleteheader_command;

/*
 * Operations
 */

enum ext_imap4flags_opcode {
	EXT_EDITHEADER_OPERATION_ADDHEADER,
	EXT_EDITHEADER_OPERATION_DELETEHEADER,
};

extern const struct sieve_operation_def addheader_operation;
extern const struct sieve_operation_def deleteheader_operation;

/*
 * Extension
 */

extern const struct sieve_extension_def editheader_extension;

int ext_editheader_load(const struct sieve_extension *ext, void **context_r);
void ext_editheader_unload(const struct sieve_extension *ext);

/*
 * Protected headers
 */

bool ext_editheader_header_allow_add(const struct sieve_extension *ext,
				     const char *hname);
bool ext_editheader_header_allow_delete(const struct sieve_extension *ext,
					const char *hname);

/*
 * Limits
 */

bool ext_editheader_header_too_large(const struct sieve_extension *ext,
				     size_t size);

#endif
