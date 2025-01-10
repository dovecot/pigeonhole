#ifndef SIEVE_EXT_IMAP4FLAGS_H
#define SIEVE_EXT_IMAP4FLAGS_H

struct sieve_variable_storage;

/*
 * Imap4flags extension
 */

/* FIXME: this is not suitable for future plugin support */

extern const struct sieve_extension_def imap4flags_extension;
extern const struct sieve_interpreter_extension
	imap4flags_interpreter_extension;

static inline int
sieve_ext_imap4flags_require_extension(struct sieve_instance *svinst,
				       const struct sieve_extension **ext_r)
{
	return sieve_extension_require(svinst, &imap4flags_extension, TRUE,
				       ext_r);
}

void sieve_ext_imap4flags_interpreter_load(
	const struct sieve_extension *ext,
	const struct sieve_runtime_env *renv);

/*
 * Action side-effect
 */

void sieve_ext_imap4flags_register_side_effect(
	struct sieve_validator *valdtr, const struct sieve_extension *flg_ext,
        const char *command);

/*
 * Flag syntax
 */

bool sieve_ext_imap4flags_flag_is_valid(const char *flag);

/*
 * Flag manipulation
 */

int sieve_ext_imap4flags_set_flags(const struct sieve_runtime_env *renv,
				   const struct sieve_extension *flg_ext,
				   struct sieve_variable_storage *storage,
				   unsigned int var_index,
				   struct sieve_stringlist *flags) ATTR_NULL(3);
int sieve_ext_imap4flags_add_flags(const struct sieve_runtime_env *renv,
				   const struct sieve_extension *flg_ext,
				   struct sieve_variable_storage *storage,
				   unsigned int var_index,
				   struct sieve_stringlist *flags) ATTR_NULL(3);
int sieve_ext_imap4flags_remove_flags(const struct sieve_runtime_env *renv,
				      const struct sieve_extension *flg_ext,
				      struct sieve_variable_storage *storage,
				      unsigned int var_index,
				      struct sieve_stringlist *flags)
				      ATTR_NULL(3);

/*
 * Flag retrieval
 */

struct sieve_stringlist *
sieve_ext_imap4flags_get_flags(const struct sieve_runtime_env *renv,
			       const struct sieve_extension *flg_ext,
			       struct sieve_stringlist *flags_list);

#endif
