/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SIEVE_SCRIPT_FILE_H
#define __SIEVE_SCRIPT_FILE_H

/*
 * Sieve script filenames
 */

bool sieve_scriptfile_has_extension(const char *filename);
const char *sieve_scriptfile_get_script_name(const char *filename);
const char *sieve_scriptfile_from_name(const char *name);

/*
 * File script specific functions
 */


/* Return directory where script resides in. Returns NULL if this is not a file
 * script.
 */
const char *sieve_file_script_get_dirpath
	(const struct sieve_script *script);

/* Return full path to file script. Returns NULL if this is not a file script.
 */
const char *sieve_file_script_get_path
	(const struct sieve_script *script);

#endif /* __SIEVE_SCRIPT_FILE_H */
