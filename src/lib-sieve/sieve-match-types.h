#ifndef __SIEVE_MATCH_TYPES_H
#define __SIEVE_MATCH_TYPES_H

#include "sieve-common.h"

enum sieve_match_type_code {
	SIEVE_MATCH_TYPE_IS,
	SIEVE_MATCH_TYPE_CONTAINS,
	SIEVE_MATCH_TYPE_MATCHES,
	SIEVE_MATCH_TYPE_CUSTOM
};

struct sieve_match_context {
    const struct sieve_match_type *match_type;
    const struct sieve_comparator *comparator;
    struct sieve_coded_stringlist *key_list;

    void *data;
};

struct sieve_match_type;
struct sieve_match_type_extension;
struct sieve_match_type_context;

struct sieve_match_type {
	const char *identifier;

	/* Match function called for every key value or should it be called once
	 * for every tested value? (TRUE = first alternative)
	 */
	bool is_iterative;
	
	const struct sieve_match_type_extension *extension;
	unsigned int code;
	
	bool (*validate)
		(struct sieve_validator *validator, struct sieve_ast_argument **arg, 
			struct sieve_match_type_context *ctx);
	bool (*validate_context)
		(struct sieve_validator *validator, struct sieve_ast_argument *arg, 
			struct sieve_match_type_context *ctx, struct sieve_ast_argument *key_arg);
			
	void (*match_init)(struct sieve_match_context *mctx);
	bool (*match)
		(struct sieve_match_context *mctx, const char *val, size_t val_size, 
			const char *key, size_t key_size, int key_index);
	bool (*match_deinit)(struct sieve_match_context *mctx);
};

struct sieve_match_type_extension {
	const struct sieve_extension *extension;

	struct sieve_extension_obj_registry match_types;
};

#define SIEVE_EXT_DEFINE_MATCH_TYPE(OP) SIEVE_EXT_DEFINE_OBJECT(OP)
#define SIEVE_EXT_DEFINE_MATCH_TYPES(OPS) SIEVE_EXT_DEFINE_OBJECTS(OPS)

struct sieve_match_type_context {
	struct sieve_command_context *command_ctx;
	const struct sieve_match_type *match_type;
	
	int ext_id;
	
	/* Context data could be used in the future to pass data between validator and
	 * generator in match types that use extra parameters. Currently not 
	 * necessary, not even for the relational extension.
	 */
	void *ctx_data;
};

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
	const struct sieve_match_type *addrp, int ext_id);
const struct sieve_match_type *sieve_match_type_find
	(struct sieve_validator *validator, const char *identifier,
		int *ext_id);
void sieve_match_type_extension_set
	(struct sieve_binary *sbin, int ext_id,
		const struct sieve_match_type_extension *ext);

extern const struct sieve_argument match_type_tag;

extern const struct sieve_match_type is_match_type;
extern const struct sieve_match_type contains_match_type;
extern const struct sieve_match_type matches_match_type;

extern const struct sieve_match_type *sieve_core_match_types[];
extern const unsigned int sieve_core_match_types_count;

const struct sieve_match_type *sieve_opr_match_type_read
	(struct sieve_binary *sbin, sieve_size_t *address);
bool sieve_opr_match_type_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
		
/* Match Utility */

struct sieve_match_context *sieve_match_begin
(const struct sieve_match_type *mtch, const struct sieve_comparator *cmp,
    struct sieve_coded_stringlist *key_list);
bool sieve_match_value
    (struct sieve_match_context *mctx, const char *value, size_t val_size);
bool sieve_match_end(struct sieve_match_context *mctx);
		
#endif /* __SIEVE_MATCH_TYPES_H */
