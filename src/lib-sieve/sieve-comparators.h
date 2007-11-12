#ifndef __SIEVE_COMPARATORS_H
#define __SIEVE_COMPARATORS_H

enum sieve_comparator_code {
	SIEVE_COMPARATOR_I_OCTET,
	SIEVE_COMPARATOR_I_ASCII_CASEMAP,
	SIEVE_COMPARATOR_CUSTOM
};

struct sieve_comparator {
	const char *identifier;
	
	enum sieve_comparator_code code;
	const struct sieve_extension *extension;
	
	/* Equality, ordering, prefix and substring match */
	
	/* ( output similar to strncmp ) */
	int (*compare)(const void *val1, size_t val1_size, 
		const void *val2, size_t val2_size);
};

struct sieve_comparator_extension {
	const struct sieve_comparator *comparator;
	const struct sieve_comparator *(*read)
		(struct sieve_binary *sbin, sieve_size_t *address);
};

void sieve_comparators_link_tag
	(struct sieve_validator *validator, 
		struct sieve_command_registration *cmd_reg,	
		unsigned int id_code);

const struct sieve_comparator i_octet_comparator;
const struct sieve_comparator i_ascii_casemap_comparator;

void sieve_comparator_register
	(struct sieve_validator *validator, const struct sieve_comparator *cmp);
const struct sieve_comparator *sieve_comparator_find
		(struct sieve_validator *validator, const char *cmp_name);

const struct sieve_comparator *sieve_opr_comparator_read
  (struct sieve_binary *sbin, sieve_size_t *address);
bool sieve_opr_comparator_dump
	(struct sieve_binary *sbin, sieve_size_t *address);

#endif /* __SIEVE_COMPARATORS_H */
