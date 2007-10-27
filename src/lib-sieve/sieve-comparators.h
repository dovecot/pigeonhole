#ifndef __SIEVE_COMPARATORS_H
#define __SIEVE_COMPARATORS_H

enum sieve_comparator_code {
	SCI_I_OCTET,
	SCI_I_ASCII_CASEMAP,
	
	SCI_CUSTOM
};

struct sieve_comparator {
	enum sieve_comparator_code code;
	const char *identifier;
	
	/* Equality, ordering, prefix and substring match */
	
	/* ( output similar to strncmp ) */
	int (*compare)(const void *val1, size_t val1_size, const void *val2, size_t val2_size);
};

extern const struct sieve_argument comparator_tag;

extern const struct sieve_comparator *sieve_core_comparators[];
extern const unsigned int sieve_core_comparators_count;


#endif /* __SIEVE_COMPARATORS_H */
