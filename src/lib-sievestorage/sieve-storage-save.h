/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SIEVE_SAVE_H
#define __SIEVE_SAVE_H

#include "sieve-storage.h"

struct sieve_save_context;

struct sieve_save_context *
sieve_storage_save_init(struct sieve_storage *storage,
	const char *scriptname, struct istream *input);

int sieve_storage_save_continue(struct sieve_save_context *ctx);

int sieve_storage_save_finish(struct sieve_save_context *ctx);

struct sieve_script *sieve_storage_save_get_tempscript
  (struct sieve_save_context *ctx);

bool sieve_storage_save_will_activate
	(struct sieve_save_context *ctx);

void sieve_storage_save_set_mtime
	(struct sieve_save_context *ctx, time_t mtime);

void sieve_storage_save_cancel(struct sieve_save_context **ctx);

int sieve_storage_save_commit(struct sieve_save_context **ctx);

/* Saves input directly as a regular file at the active script path.
 * This is needed for the doveadm-sieve plugin.
 */
int sieve_storage_save_as_active_script(struct sieve_storage *storage,
	struct istream *input, time_t mtime);

#endif

