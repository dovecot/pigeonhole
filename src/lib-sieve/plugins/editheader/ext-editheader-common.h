/* Copyright (c) 2002-2011 Pigeonhole authors, see the included COPYING file
 */

#ifndef __EXT_EDITHEADER_COMMON_H
#define __EXT_EDITHEADER_COMMON_H

/*
 * Extensions
 */

extern const struct sieve_extension_def editheader_extension;

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
	EXT_EDITHEADER_OPERATION_DELETEHEADER
};

extern const struct sieve_operation_def addheader_operation;
extern const struct sieve_operation_def deleteheader_operation;


#endif /* __EXT_EDITHEADER_COMMON_H */
