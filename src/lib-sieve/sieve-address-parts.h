#ifndef __SIEVE_ADDRESS_PARTS_H
#define __SIEVE_ADDRESS_PARTS_H

#include "message-address.h"

#include "sieve-common.h"

enum sieve_address_part_code {
	SIEVE_ADDRESS_PART_ALL,
	SIEVE_ADDRESS_PART_LOCAL,
	SIEVE_ADDRESS_PART_DOMAIN,
	SIEVE_ADDRESS_PART_CUSTOM
};

struct sieve_address_part {
	const char *identifier;
	const struct sieve_argument *tag;
	
	enum sieve_address_part_code code;
	const struct sieve_extension *extension;

	const char *(*extract_from)(const struct message_address *address);
};

void sieve_address_parts_link_tags
	(struct sieve_validator *validator, 
		struct sieve_command_registration *cmd_reg,
		unsigned int id_code);
		
void sieve_address_part_register
	(struct sieve_validator *validator, const struct sieve_address_part *addrp); 
const struct sieve_address_part *sieve_address_part_find
		(struct sieve_validator *validator, const char *addrp_name);

const struct sieve_address_part all_address_part;
const struct sieve_address_part local_address_part;
const struct sieve_address_part domain_address_part;

extern const struct sieve_address_part *sieve_core_address_parts[];
extern const unsigned int sieve_core_address_parts_count;

const struct sieve_address_part *sieve_opr_address_part_read
  (struct sieve_binary *sbin, sieve_size_t *address);
bool sieve_opr_address_part_dump
	(struct sieve_binary *sbin, sieve_size_t *address);

bool sieve_address_stringlist_match
	(struct sieve_address_part *addrp, struct sieve_coded_stringlist *key_list,
		struct sieve_comparator *cmp, const char *data);

#endif /* __SIEVE_ADDRESS_PARTS_H */
