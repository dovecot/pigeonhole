#ifndef SIEVE_ADDRESS_H
#define SIEVE_ADDRESS_H

#include "lib.h"
#include "strfuncs.h"
#include "smtp-address.h"

#include "sieve-common.h"
#include "sieve-stringlist.h"

/*
 * Address list API
 */

struct sieve_address_list {
	struct sieve_stringlist strlist;

	int (*next_item)(struct sieve_address_list *_addrlist,
			 struct smtp_address *addr_r, string_t **unparsed_r);
};

static inline int
sieve_address_list_next_item(struct sieve_address_list *addrlist,
			     struct smtp_address *addr_r, string_t **unparsed_r)
{
	return addrlist->next_item(addrlist, addr_r, unparsed_r);
}

static inline void
sieve_address_list_reset(struct sieve_address_list *addrlist)
{
	sieve_stringlist_reset(&addrlist->strlist);
}

static inline int
sieve_address_list_get_length(struct sieve_address_list *addrlist)
{
	return sieve_stringlist_get_length(&addrlist->strlist);
}

static inline void
sieve_address_list_set_trace(struct sieve_address_list *addrlist, bool trace)
{
	sieve_stringlist_set_trace(&addrlist->strlist, trace);
}

/*
 * Header address list
 */

struct sieve_address_list *
sieve_header_address_list_create(const struct sieve_runtime_env *renv,
				 struct sieve_stringlist *field_values);

/*
 * Sieve address parsing/validatin
 */

const struct smtp_address *
sieve_address_parse(const char *address, const char **error_r);
const struct smtp_address *
sieve_address_parse_str(string_t *address, const char **error_r);

bool sieve_address_validate(const char *address, const char **error_r);
bool sieve_address_validate_str(string_t *address, const char **error_r);

#endif
