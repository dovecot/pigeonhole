/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SIEVE_EXT_IMAP4FLAGS_H
#define __SIEVE_EXT_IMAP4FLAGS_H

struct sieve_variable_storage;

/*
 * Imap4flags extension
 */

/* FIXME: this is not suitable for future plugin support */

extern const struct sieve_extension_def imap4flags_extension;

static inline const struct sieve_extension *
sieve_ext_imap4flags_require_extension
(struct sieve_instance *svinst)
{
	return sieve_extension_require
		(svinst, &imap4flags_extension, TRUE);
}

/*
 * Action side-effect
 */

void sieve_ext_imap4flags_register_side_effect
(struct sieve_validator *valdtr, const struct sieve_extension *flg_ext,
        const char *command);

/*
 * Flag syntax
 */

bool sieve_ext_imap4flags_flag_is_valid(const char *flag);

/*
 * Flag manipulation
 */

int sieve_ext_imap4flags_set_flags
(const struct sieve_runtime_env *renv,
	const struct sieve_extension *flg_ext,
	struct sieve_variable_storage *storage,
	unsigned int var_index,
	struct sieve_stringlist *flags) ATTR_NULL(3);
int sieve_ext_imap4flags_add_flags
(const struct sieve_runtime_env *renv,
	const struct sieve_extension *flg_ext,
	struct sieve_variable_storage *storage,
	unsigned int var_index,
	struct sieve_stringlist *flags) ATTR_NULL(3);
int sieve_ext_imap4flags_remove_flags
(const struct sieve_runtime_env *renv,
	const struct sieve_extension *flg_ext,
	struct sieve_variable_storage *storage,
	unsigned int var_index,
	struct sieve_stringlist *flags) ATTR_NULL(3);

/*
 * Flag retrieval
 */

struct sieve_stringlist *sieve_ext_imap4flags_get_flags
(const struct sieve_runtime_env *renv,
	const struct sieve_extension *flg_ext,
	struct sieve_stringlist *flags_list);

#endif
