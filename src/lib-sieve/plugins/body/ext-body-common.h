/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */
 
#ifndef __EXT_BODY_COMMON_H
#define __EXT_BODY_COMMON_H

/*
 * Extension
 */
 
extern const struct sieve_extension_def body_extension;

/* 
 * Commands
 */

extern const struct sieve_command_def body_test;
 
/*
 * Operations
 */

extern const struct sieve_operation_def body_operation;

/*
 * Message body part extraction
 */

struct ext_body_part {
	const char *content;
	unsigned long size;
};

bool ext_body_get_content
	(const struct sieve_runtime_env *renv, const char * const *content_types,
		int decode_to_plain, struct ext_body_part **parts_r);

bool ext_body_get_raw
	(const struct sieve_runtime_env *renv, struct ext_body_part **parts_r);

#endif /* __EXT_BODY_COMMON_H */
