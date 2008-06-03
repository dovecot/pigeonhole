#ifndef __SIEVE_SCRIPT_H
#define __SIEVE_SCRIPT_H

#include "sieve-common.h"

struct sieve_script;

struct sieve_script *sieve_script_create
	(const char *path, const char *name, 
		struct sieve_error_handler *ehandler, bool *exists_r);

void sieve_script_ref(struct sieve_script *script);
void sieve_script_unref(struct sieve_script **script);

/* Stream manageement */

struct istream *sieve_script_open(struct sieve_script *script, bool *deleted_r);
void sieve_script_close(struct sieve_script *script);

uoff_t sieve_script_get_size(struct sieve_script *script);

int sieve_script_cmp
	(struct sieve_script *script1, struct sieve_script *script2);
unsigned int sieve_script_hash(struct sieve_script *script);
bool sieve_script_older(struct sieve_script *script, time_t time);

static inline bool sieve_script_equals
	(struct sieve_script *script1, struct sieve_script *script2)
{
	return ( sieve_script_cmp(script1, script2) == 0 );
}

const char *sieve_script_name(struct sieve_script *script);
const char *sieve_script_filename(struct sieve_script *script);
const char *sieve_script_path(struct sieve_script *script);
const char *sieve_script_binpath(struct sieve_script *script);

#endif /* __SIEVE_SCRIPT_H */
