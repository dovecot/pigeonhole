#ifndef __SIEVE_SCRIPT_H
#define __SIEVE_SCRIPT_H

#include "sieve-common.h"

/* FIXME: This conflicts with the managesieve implementation */

struct sieve_script;

struct sieve_script *sieve_script_create(const char *path, const char *name);

void sieve_script_ref(struct sieve_script *script);
void sieve_script_unref(struct sieve_script **script);

/* Stream manageement */

struct istream *sieve_script_open(struct sieve_script *script, 
	struct sieve_error_handler *ehandler);
void sieve_script_close(struct sieve_script *script);

bool sieve_script_equals
(struct sieve_script *script1, struct sieve_script *script2);

inline const char *sieve_script_name(struct sieve_script *script);
inline const char *sieve_script_path(struct sieve_script *script);

#endif /* __SIEVE_SCRIPT_H */
