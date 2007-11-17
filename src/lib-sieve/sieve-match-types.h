#ifndef __SIEVE_MATCH_TYPES_H
#define __SIEVE_MATCH_TYPES_H

#include "sieve-common.h"

enum sieve_match_type_code {
	SIEVE_MATCH_TYPE_IS,
	SIEVE_MATCH_TYPE_CONTAINS,
	SIEVE_MATCH_TYPE_MATCHES,
	SIEVE_MATCH_TYPE_CUSTOM
};

struct sieve_match_type_extension;

struct sieve_match_type {
	const char *identifier;
	
	enum sieve_match_type_code code;
	
	const struct sieve_match_type_extension *extension;
	unsigned int ext_code;
};

struct sieve_match_type_extension {
	const struct sieve_extension *extension;

	/* Either a single match-type in this extension ... */
	const struct sieve_match_type *match_type;
	
	/* ... or multiple: then the extension must handle emit/read */
	const struct sieve_match_type *(*get_part)
		(unsigned int code);
};

void sieve_match_types_link_tags
	(struct sieve_validator *validator, 
		struct sieve_command_registration *cmd_reg,
		unsigned int id_code);
		
void sieve_match_type_register
	(struct sieve_validator *validator, 
	const struct sieve_match_type *addrp, int ext_id);
const struct sieve_match_type *sieve_match_type_find
	(struct sieve_validator *validator, const char *identifier,
		int *ext_id);
void sieve_match_type_extension_set
	(struct sieve_interpreter *interpreter, int ext_id,
		const struct sieve_match_type_extension *ext);

extern const struct sieve_argument match_type_tag;

extern const struct sieve_match_type is_match_type;
extern const struct sieve_match_type contains_match_type;
extern const struct sieve_match_type matches_match_type;

extern const struct sieve_match_type *sieve_core_match_types[];
extern const unsigned int sieve_core_match_types_count;

const struct sieve_match_type *sieve_opr_match_type_read
  (struct sieve_interpreter *interpreter, 
  	struct sieve_binary *sbin, sieve_size_t *address);
bool sieve_opr_match_type_dump
	(struct sieve_interpreter *interpreter,
		struct sieve_binary *sbin, sieve_size_t *address);

#endif /* __SIEVE_MATCH_TYPES_H */
