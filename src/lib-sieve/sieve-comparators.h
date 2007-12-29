#ifndef __SIEVE_COMPARATORS_H
#define __SIEVE_COMPARATORS_H

#include "sieve-common.h"
#include "sieve-extensions.h"

enum sieve_comparator_code {
	SIEVE_COMPARATOR_I_OCTET,
	SIEVE_COMPARATOR_I_ASCII_CASEMAP,
	SIEVE_COMPARATOR_CUSTOM
};

enum sieve_comparator_flags {
	SIEVE_COMPARATOR_FLAG_ORDERING = (1 << 0),
	SIEVE_COMPARATOR_FLAG_EQUALITY = (1 << 1),
	SIEVE_COMPARATOR_FLAG_PREFIX_MATCH = (1 << 2),
	SIEVE_COMPARATOR_FLAG_SUBSTRING_MATCH = (1 << 3),	
};

struct sieve_comparator {
	const char *identifier;
	
	unsigned int flags;
	
	const struct sieve_comparator_extension *extension;
	unsigned int code;
	
	/* Equality and ordering */

	int (*compare)(const struct sieve_comparator *cmp, 
		const char *val1, size_t val1_size, 
		const char *val2, size_t val2_size);
	
	/* Prefix and substring match */
	
	bool (*char_match)(const struct sieve_comparator *cmp, 
		const char **val, const char *val_end,
		const char **key, const char *key_end);
	bool (*char_skip)(const struct sieve_comparator *cmp, 
		const char **val, const char *val_end);
};

struct sieve_comparator_extension {
	const struct sieve_extension *extension;
	
	struct sieve_extension_obj_registry comparators;
};

#define SIEVE_EXT_DEFINE_COMPARATOR(OP) SIEVE_EXT_DEFINE_OBJECT(OP)
#define SIEVE_EXT_DEFINE_COMPARATORS(OPS) SIEVE_EXT_DEFINE_OBJECTS(OPS)

struct sieve_comparator_context {
	struct sieve_command_context *command_ctx;
	const struct sieve_comparator *comparator;
	
	int ext_id;
};

extern const struct sieve_argument comparator_tag;

void sieve_comparators_link_tag
	(struct sieve_validator *validator, 
		struct sieve_command_registration *cmd_reg,	int id_code);
inline bool sieve_comparator_tag_is
	(struct sieve_ast_argument *tag, const struct sieve_comparator *cmp);
inline const struct sieve_comparator *sieve_comparator_tag_get
	(struct sieve_ast_argument *tag);

extern const struct sieve_comparator i_octet_comparator;
extern const struct sieve_comparator i_ascii_casemap_comparator;

void sieve_comparator_register
	(struct sieve_validator *validator, 
	const struct sieve_comparator *cmp, int ext_id); 
const struct sieve_comparator *sieve_comparator_find
	(struct sieve_validator *validator, const char *identifier,
		int *ext_id);

const struct sieve_comparator *sieve_opr_comparator_read
	(struct sieve_binary *sbin, sieve_size_t *address);
bool sieve_opr_comparator_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);

void sieve_comparator_extension_set
	(struct sieve_binary *sbin, int ext_id,
		const struct sieve_comparator_extension *ext);

#endif /* __SIEVE_COMPARATORS_H */
