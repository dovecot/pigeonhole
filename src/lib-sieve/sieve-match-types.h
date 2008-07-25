#ifndef __SIEVE_MATCH_TYPES_H
#define __SIEVE_MATCH_TYPES_H

#include "sieve-common.h"
#include "sieve-extensions.h"
#include "sieve-code.h"
#include "sieve-objects.h"

/*
 * Core match types 
 */
 
enum sieve_match_type_code {
	SIEVE_MATCH_TYPE_IS,
	SIEVE_MATCH_TYPE_CONTAINS,
	SIEVE_MATCH_TYPE_MATCHES,
	SIEVE_MATCH_TYPE_CUSTOM
};

extern const struct sieve_match_type is_match_type;
extern const struct sieve_match_type contains_match_type;
extern const struct sieve_match_type matches_match_type;

struct sieve_match_type;
struct sieve_match_type_context;

struct sieve_match_type {
	struct sieve_object object;

	/* Match function called for every key value or should it be called once
	 * for every tested value? (TRUE = first alternative)
	 */
	bool is_iterative;
		
	bool (*validate)
		(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
			struct sieve_match_type_context *ctx);
	bool (*validate_context)
		(struct sieve_validator *validator, struct sieve_ast_argument *arg, 
			struct sieve_match_type_context *ctx, struct sieve_ast_argument *key_arg);
			
	void (*match_init)(struct sieve_match_context *mctx);
	int (*match)
		(struct sieve_match_context *mctx, const char *val, size_t val_size, 
			const char *key, size_t key_size, int key_index);
	int (*match_deinit)(struct sieve_match_context *mctx);
};

struct sieve_match_type_context {
	struct sieve_command_context *command_ctx;
	const struct sieve_match_type *match_type;
	
	/* Context data could be used in the future to pass data between validator and
	 * generator in match types that use extra parameters. Currently not 
	 * necessary, not even for the relational extension.
	 */
	void *ctx_data;
};

/* Match values */

struct sieve_match_values;

bool sieve_match_values_set_enabled
	(struct sieve_interpreter *interp, bool enable);
struct sieve_match_values *sieve_match_values_start
	(struct sieve_interpreter *interp);
void sieve_match_values_add
	(struct sieve_match_values *mvalues, string_t *value);
void sieve_match_values_add_char
	(struct sieve_match_values *mvalues, char c);	
void sieve_match_values_skip
	(struct sieve_match_values *mvalues, int num);
void sieve_match_values_get
	(struct sieve_interpreter *interp, unsigned int index, string_t **value_r);

/* ... */

void sieve_match_types_link_tags
	(struct sieve_validator *validator, 
		struct sieve_command_registration *cmd_reg, int id_code);
bool sieve_match_type_validate_argument
(struct sieve_validator *validator, struct sieve_ast_argument *arg,
	struct sieve_ast_argument *key_arg);
bool sieve_match_type_validate
(struct sieve_validator *validator, struct sieve_command_context *cmd,
	struct sieve_ast_argument *key_arg);
		
void sieve_match_type_register
	(struct sieve_validator *validator, 
	const struct sieve_match_type *addrp);
const struct sieve_match_type *sieve_match_type_find
	(struct sieve_validator *validator, const char *identifier);

extern const struct sieve_argument match_type_tag;

/*
 * Match type operand
 */
 
const struct sieve_operand match_type_operand;
struct sieve_operand_class sieve_match_type_operand_class;

#define SIEVE_EXT_DEFINE_MATCH_TYPE(OP) SIEVE_EXT_DEFINE_OBJECT(OP)
#define SIEVE_EXT_DEFINE_MATCH_TYPES(OPS) SIEVE_EXT_DEFINE_OBJECTS(OPS)

static inline bool sieve_operand_is_match_type
(const struct sieve_operand *operand)
{
	return ( operand != NULL && 
		operand->class == &sieve_match_type_operand_class );
}

static inline void sieve_opr_match_type_emit
(struct sieve_binary *sbin, const struct sieve_match_type *mtch)
{ 
	sieve_opr_object_emit(sbin, &mtch->object);
}

static inline const struct sieve_match_type *sieve_opr_match_type_read
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{
	return (const struct sieve_match_type *) sieve_opr_object_read
		(renv, &sieve_match_type_operand_class, address);
}

static inline bool sieve_opr_match_type_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	return sieve_opr_object_dump
		(denv, &sieve_match_type_operand_class, address, NULL);
}

/* Match Utility */

bool sieve_match_substring_validate_context
(struct sieve_validator *validator, struct sieve_ast_argument *arg,
    struct sieve_match_type_context *ctx, 
	struct sieve_ast_argument *key_arg);

#endif /* __SIEVE_MATCH_TYPES_H */
