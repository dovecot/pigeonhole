/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SIEVE_STORAGE_SCRIPT_H
#define __SIEVE_STORAGE_SCRIPT_H

#include "sieve-script.h"

#include "sieve-storage.h"

struct sieve_script *sieve_storage_script_init
	(struct sieve_storage *storage, const char *scriptname);

int sieve_storage_active_script_get_file
	(struct sieve_storage *storage, const char **file_r);
int sieve_storage_active_script_get_name
	(struct sieve_storage *storage, const char **name_r);
const char *sieve_storage_active_script_get_path
	(struct sieve_storage *storage);
int sieve_storage_active_script_is_no_link(struct sieve_storage *storage);
struct sieve_script *sieve_storage_active_script_get
	(struct sieve_storage *storage);
int sieve_storage_active_script_get_last_change
	(struct sieve_storage *storage, time_t *last_change_r);

int sieve_storage_deactivate(struct sieve_storage *storage, time_t mtime);
int sieve_storage_script_is_active(struct sieve_script *script);
int sieve_storage_script_activate(struct sieve_script *script, time_t mtime);
int sieve_storage_script_delete(struct sieve_script **script);
int sieve_storage_script_rename
	(struct sieve_script *script, const char *newname);

#endif

