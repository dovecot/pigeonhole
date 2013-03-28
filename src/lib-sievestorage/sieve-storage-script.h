/* Copyright (c) 2002-2012 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SIEVE_STORAGE_SCRIPT_H
#define __SIEVE_STORAGE_SCRIPT_H

#include "sieve-script.h"

#include "sieve-storage.h"

struct sieve_script *sieve_storage_script_init
	(struct sieve_storage *storage, const char *scriptname);

int sieve_storage_get_active_scriptfile
	(struct sieve_storage *storage, const char **file_r);
int sieve_storage_get_active_scriptname
	(struct sieve_storage *storage, const char **name_r);

struct sieve_script *sieve_storage_get_active_script
	(struct sieve_storage *storage);
int sieve_storage_get_active_script_last_change
	(struct sieve_storage *storage, time_t *last_change_r);

int sieve_storage_script_is_active(struct sieve_script *script);

int sieve_storage_script_delete(struct sieve_script **script);

int sieve_storage_deactivate(struct sieve_storage *storage);

int sieve_storage_script_activate(struct sieve_script *script);

int sieve_storage_script_rename
(struct sieve_script *script, const char *newname);

#endif

