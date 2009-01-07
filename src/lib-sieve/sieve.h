/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file 
 */

#ifndef __SIEVE_H
#define __SIEVE_H

#include <stdio.h>

struct sieve_script;
struct sieve_binary;

#include "sieve-config.h"
#include "sieve-types.h"
#include "sieve-error.h"

/*
 * Main Sieve library interface
 */

/* sieve_init(): 
 *   Initializes the sieve engine. Must be called before any sieve functionality
 *   is used.
 */
bool sieve_init(void);

/* sieve_deinit():
 *   Frees all memory allocated by the sieve engine. 
 */
void sieve_deinit(void);

/* sieve_get_capabilities():
 *
 */
const char *sieve_get_capabilities(const char *name);

/* sieve_set_extensions():
 *
 */
void sieve_set_extensions(const char *extensions);

/*
 * Script compilation
 */

/* sieve_compile_script:
 */
struct sieve_binary *sieve_compile_script
	(struct sieve_script *script, struct sieve_error_handler *ehandler);

/* sieve_compile:
 *
 *   Compiles the script into a binary.
 */
struct sieve_binary *sieve_compile
	(const char *script_path, const char *script_name, 
		struct sieve_error_handler *ehandler);

/* 
 * Reading/writing Sieve binaries
 */

/* sieve_open:
 *
 *   First tries to open the binary version of the specified script and
 *   if it does not exist or if it contains errors, the script is
 *   (re-)compiled. The binary is updated if the script is recompiled.
 *   Note that errors in the bytecode are not caught here.
 *
 */
struct sieve_binary *sieve_open
	(const char *scriptpath, const char *script_name, 
		struct sieve_error_handler *ehandler, bool *exists_r);

/* sieve_save:
 *  Saves the binary as the file indicated by the path parameter.
 */
bool sieve_save
    (struct sieve_binary *sbin, const char *path);

/* sieve_close:
 *
 *   Closes a compiled/opened sieve binary.
 */
void sieve_close(struct sieve_binary **sbin);

/*
 * Debugging
 */

/* sieve_dump:
 *
 *   Dumps the byte code in human-readable form to the specified ostream.
 */
void sieve_dump(struct sieve_binary *sbin, struct ostream *stream);

/* sieve_test:
 *
 *   Executes the bytecode, but only prints the result to the given stream.
 */ 
int sieve_test
	(struct sieve_binary *sbin, const struct sieve_message_data *msgdata, 
		const struct sieve_script_env *senv, struct sieve_error_handler *ehandler, 
		struct ostream *stream);

/*
 * Script execution
 */

/* sieve_execute:
 *
 *   Executes the binary, including the result.  
 */
int sieve_execute
	(struct sieve_binary *sbin, const struct sieve_message_data *msgdata,
		const struct sieve_script_env *senv, struct sieve_error_handler *ehandler);
		
/*
 * Multiscript support
 */
 
struct sieve_multiscript;
 
struct sieve_multiscript *sieve_multiscript_start
	(const struct sieve_message_data *msgdata, const struct sieve_script_env *senv,
		struct sieve_error_handler *ehandler);
		
bool sieve_multiscript_test
	(struct sieve_multiscript *mscript, struct sieve_binary *sbin, 
		bool final, struct ostream *stream);
bool sieve_multiscript_execute
	(struct sieve_multiscript *mscript, struct sieve_binary *sbin,
		bool final);
	
int sieve_multiscript_finish(struct sieve_multiscript **mscript);

#endif
