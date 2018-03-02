#ifndef EXT_METADATA_COMMON_H
#define EXT_METADATA_COMMON_H

#include "lib.h"
#include "mail-storage.h"
#include "imap-metadata.h"

#include "sieve-common.h"

/*
 * Extension
 */

extern const struct sieve_extension_def mboxmetadata_extension;
extern const struct sieve_extension_def servermetadata_extension;

/*
 * Commands
 */

extern const struct sieve_command_def metadata_test;
extern const struct sieve_command_def servermetadata_test;
extern const struct sieve_command_def metadataexists_test;
extern const struct sieve_command_def servermetadataexists_test;

/*
 * Operations
 */

enum ext_metadata_opcode {
	EXT_METADATA_OPERATION_METADATA,
	EXT_METADATA_OPERATION_METADATAEXISTS
};

extern const struct sieve_operation_def metadata_operation;
extern const struct sieve_operation_def servermetadata_operation;
extern const struct sieve_operation_def metadataexists_operation;
extern const struct sieve_operation_def servermetadataexists_operation;

#endif
