#ifndef EXT_BODY_COMMON_H
#define EXT_BODY_COMMON_H

/*
 * Types
 */

enum tst_body_transform {
	TST_BODY_TRANSFORM_RAW,
	TST_BODY_TRANSFORM_CONTENT,
	TST_BODY_TRANSFORM_TEXT
};

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

int ext_body_get_part_list(const struct sieve_runtime_env *renv,
			   enum tst_body_transform transform,
			   const char *const *content_types,
			   struct sieve_stringlist **strlist_r);

#endif
