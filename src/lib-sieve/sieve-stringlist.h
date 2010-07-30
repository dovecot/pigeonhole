#ifndef __SIEVE_STRINGLIST_H
#define __SIEVE_STRINGLIST_H

/*
 * Stringlist API
 */

struct sieve_stringlist {
	const struct sieve_runtime_env *runenv;

	int (*next_item)
		(struct sieve_stringlist *strlist, string_t **str_r);
	void (*reset)
		(struct sieve_stringlist *strlist);
	int (*get_length)
		(struct sieve_stringlist *strlist);

	bool (*read_all)
		(struct sieve_stringlist *strlist, pool_t pool,
			const char * const **list_r);
};

static inline int sieve_stringlist_next_item
(struct sieve_stringlist *strlist, string_t **str_r) 
{
	return strlist->next_item(strlist, str_r);
}

static inline void sieve_stringlist_reset
(struct sieve_stringlist *strlist) 
{
	return strlist->reset(strlist);
}

int sieve_stringlist_get_length
	(struct sieve_stringlist *strlist);

bool sieve_stringlist_read_all
	(struct sieve_stringlist *strlist, pool_t pool,
		const char * const **list_r);

/*
 * Single Stringlist
 */

struct sieve_stringlist *sieve_single_stringlist_create
	(const struct sieve_runtime_env *renv, string_t *str, bool count_empty);
struct sieve_stringlist *sieve_single_stringlist_create_cstr
(const struct sieve_runtime_env *renv, const char *cstr, bool count_empty);

#endif /* __SIEVE_STRINGLIST_H */
