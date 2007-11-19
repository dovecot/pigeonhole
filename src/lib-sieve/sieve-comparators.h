#ifndef __SIEVE_COMPARATORS_H
#define __SIEVE_COMPARATORS_H

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
	
	enum sieve_comparator_code code;
	unsigned int flags;
	
	const struct sieve_comparator_extension *extension;
	unsigned int ext_code;
	
	/* Equality and ordering */

	int (*compare)(const struct sieve_comparator *cmp, 
		const char *val1, size_t val1_size, 
		const char *val2, size_t val2_size);
	
	/* Prefix and substring match */
	
	bool (*char_match)(const struct sieve_comparator *cmp, 
		const char **val1, const char *val1_end, 
		const char **val2, const char *val2_end);
};

struct sieve_comparator_extension {
	const struct sieve_extension *extension;
	
	/* Either a single comparator in this extension ... */
	const struct sieve_comparator *comparator;
	
	/* ... or multiple: then the extension must handle emit/read */
	const struct sieve_comparator *(*get_comparator)
		(unsigned int code);
};

void sieve_comparators_link_tag
	(struct sieve_validator *validator, 
		struct sieve_command_registration *cmd_reg,	
		unsigned int id_code);

const struct sieve_comparator i_octet_comparator;
const struct sieve_comparator i_ascii_casemap_comparator;

void sieve_comparator_register
	(struct sieve_validator *validator, 
	const struct sieve_comparator *cmp, int ext_id); 
const struct sieve_comparator *sieve_comparator_find
	(struct sieve_validator *validator, const char *identifier,
		int *ext_id);

const struct sieve_comparator *sieve_opr_comparator_read
  (struct sieve_interpreter *interpreter, 
  	struct sieve_binary *sbin, sieve_size_t *address);
bool sieve_opr_comparator_dump
	(struct sieve_interpreter *interpreter,
		struct sieve_binary *sbin, sieve_size_t *address);

void sieve_comparator_extension_set
	(struct sieve_interpreter *interpreter, int ext_id,
		const struct sieve_comparator_extension *ext);

#endif /* __SIEVE_COMPARATORS_H */
