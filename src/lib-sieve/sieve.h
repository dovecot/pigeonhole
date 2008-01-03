/* Copyright (c) 2002-2007 Dovecot authors, see the included COPYING file */

#ifndef __SIEVE_H
#define __SIEVE_H

#include <stdio.h>

#include "sieve-error.h"

#define SIEVE_VERSION "0.0.1"
#define SIEVE_IMPLEMENTATION "Dovecot Sieve " SIEVE_VERSION

struct sieve_binary;

/* The mail message + envelope data */

struct sieve_message_data {
	struct mail *mail;
	const char *return_path;
	const char *to_address;
	const char *auth_user;
	const char *id;
};

/* Environment for currently executing script */

struct sieve_script_env {
	/* Mail-related */
	const char *inbox;
	struct mail_namespace *namespaces;
	
	/* System-related */
	const char *username;
	const char *hostname;
	const char *postmaster_address;
		
	/* Callbacks */
	
	/* Interface for sending mail */
	void *(*smtp_open)
		(const char *destination, const char *return_path, FILE **file_r);
	bool (*smtp_close)(void *handle);
	
	/* Interface for marking and checking duplicates */
	int (*duplicate_check)(const void *id, size_t id_size, const char *user);
	void (*duplicate_mark)(const void *id, size_t id_size,
                    const char *user, time_t time);
};	

bool sieve_init(const char *plugins);
void sieve_deinit(void);

/* sieve_compile:
 *
 *   Compiles the script into a binary. 
 */
struct sieve_binary *sieve_compile
	(const char *scriptpath, struct sieve_error_handler *ehandler);

/* sieve_open:
 * 
 *   First tries to open the binary version of the specified script and
 *   if it does not exist or if it contains errors, the script is 
 *   (re-)compiled. The binary is updated if the script is recompiled. 
 *   Note that errors in the bytecode are not caught here.
 *   
 */
struct sieve_binary *sieve_open
	(const char *scriptpath, struct sieve_error_handler *ehandler);

/* sieve_dump:
 * 
 *   Dumps the byte code in human-readable form to the specified ostream.
 */
void sieve_dump(struct sieve_binary *sbin, struct ostream *stream);

/* sieve_test:
 *
 *   Executes the bytecode, but only prints the result to stdout.
 */ 
int sieve_test
	(struct sieve_binary *sbin, const struct sieve_message_data *msgdata, 
		const struct sieve_script_env *senv, 
		struct sieve_error_handler *ehandler);

/* sieve_execute:
 *
 *   Executes the binary, including the result.  
 */
int sieve_execute
	(struct sieve_binary *sbin, const struct sieve_message_data *msgdata,
		const struct sieve_script_env *senv,
		struct sieve_error_handler *ehandler);

/* sieve_close:
 *
 *   Closes a compiled/opened sieve binary.
 */
void sieve_close(struct sieve_binary **sbin);

/* sieve_get_capabilities:
 *
 */
const char *sieve_get_capabilities(void);

#endif
