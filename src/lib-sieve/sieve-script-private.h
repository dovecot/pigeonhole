#ifndef __SIEVE_SCRIPT_PRIVATE_H
#define __SIEVE_SCRIPT_PRIVATE_H

#include "sieve-script.h"

struct sieve_script {
    pool_t pool;
    unsigned int refcount;

    struct stat st;

    struct sieve_error_handler *ehandler;

    /* Parameters */
    const char *name;
	const char *basename;
    const char *filename;
    const char *dirpath;
    const char *path;

    /* Stream */
    int fd; /* FIXME: we could use the stream's autoclose facility */
    struct istream *stream;
};

struct sieve_script *sieve_script_init
(struct sieve_script *script, const char *path, const char *name,
    struct sieve_error_handler *ehandler, bool *exists_r);

#endif /* __SIEVE_SCRIPT_PRIVATE_H */
