/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file 
 */
 
#ifndef __SIEVE_COMPARATORS_H
#define __SIEVE_COMPARATORS_H

#include "sieve-common.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-objects.h"
#include "sieve-code.h"

/* 
 * Core comparators 
 */
 
enum sieve_comparator_code {
	SIEVE_COMPARATOR_I_OCTET,
	SIEVE_COMPARATOR_I_ASCII_CASEMAP,
	SIEVE_COMPARATOR_CUSTOM
};

extern const struct sieve_comparator i_octet_comparator;
extern const struct sieve_comparator i_ascii_casemap_comparator;

/*
 * Comparator flags
 */

enum sieve_comparator_flags {
	SIEVE_COMPARATOR_FLAG_ORDERING = (1 << 0),
	SIEVE_COMPARATOR_FLAG_EQUALITY = (1 << 1),
	SIEVE_COMPARATOR_FLAG_PREFIX_MATCH = (1 << 2),
	SIEVE_COMPARATOR_FLAG_SUBSTRING_MATCH = (1 << 3),	
};

/*
 * Comparator object
 */

struct sieve_comparator {
	struct sieve_object object;	
		
	unsigned int flags;
	
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

/*
 * Comparator tagged argument
 */
 
extern const struct sieve_argument comparator_tag;

static inline bool sieve_argument_is_comparator
	(struct sieve_ast_argument *arg) 
{
	return arg->argument == &comparator_tag;
}

void sieve_comparators_link_tag
	(struct sieve_validator *validator, 
		struct sieve_command_registration *cmd_reg,	int id_code);
bool sieve_comparator_tag_is
	(struct sieve_ast_argument *tag, const struct sieve_comparator *cmp);
const struct sieve_comparator *sieve_comparator_tag_get
	(struct sieve_ast_argument *tag);

void sieve_comparator_register
	(struct sieve_validator *validator, const struct sieve_comparator *cmp); 
const struct sieve_comparator *sieve_comparator_find
	(struct sieve_validator *validator, const char *identifier);
		
/*
 * Comparator operand
 */

#define SIEVE_EXT_DEFINE_COMPARATOR(OP) SIEVE_EXT_DEFINE_OBJECT(OP)
#define SIEVE_EXT_DEFINE_COMPARATORS(OPS) SIEVE_EXT_DEFINE_OBJECTS(OPS)

extern const struct sieve_operand_class sieve_comparator_operand_class;
extern const struct sieve_operand comparator_operand;

static inline void sieve_opr_comparator_emit
(struct sieve_binary *sbin, const struct sieve_comparator *cmp)
{ 
	sieve_opr_object_emit(sbin, &cmp->object);
}

static inline const struct sieve_comparator *sieve_opr_comparator_read
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	return (const struct sieve_comparator *) sieve_opr_object_read
		(renv, &sieve_comparator_operand_class, address);
}

static inline bool sieve_opr_comparator_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	return sieve_opr_object_dump
		(denv, &sieve_comparator_operand_class, address, NULL);
}
	
/*
 * Trivial/Common comparator method implementations
 */

bool sieve_comparator_octet_skip
	(const struct sieve_comparator *cmp ATTR_UNUSED, 
		const char **val, const char *val_end);

#endif /* __SIEVE_COMPARATORS_H */
