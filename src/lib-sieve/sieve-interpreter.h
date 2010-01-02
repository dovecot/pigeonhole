/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __SIEVE_INTERPRETER_H
#define __SIEVE_INTERPRETER_H

#include "lib.h"
#include "array.h"
#include "buffer.h"
#include "mail-storage.h"

#include "sieve-common.h"
#include "sieve-code.h"

/*
 * Forward declarations
 */
 
struct sieve_interpreter;

/*
 * Runtime environment
 */

struct sieve_runtime_env {
	struct sieve_interpreter *interp;
	struct sieve_instance *svinst;

	struct sieve_binary *sbin;
	struct sieve_operation oprtn; 

	struct sieve_script *script;
	const struct sieve_script_env *scriptenv;
	
	const struct sieve_message_data *msgdata;
	struct sieve_message_context *msgctx;

	struct sieve_result *result;
	
	struct sieve_exec_status *exec_status;
	struct ostream *trace_stream;
};

/* 
 * Interpreter 
 */

struct sieve_interpreter *sieve_interpreter_create
	(struct sieve_binary *sbin, struct sieve_error_handler *ehandler);
void sieve_interpreter_free(struct sieve_interpreter **interp);

/*
 * Accessors
 */

pool_t sieve_interpreter_pool
	(struct sieve_interpreter *interp);
struct sieve_script *sieve_interpreter_script
	(struct sieve_interpreter *interp);
struct sieve_error_handler *sieve_interpreter_get_error_handler
	(struct sieve_interpreter *interp);
struct sieve_instance *sieve_interpreter_svinst
	(struct sieve_interpreter *interp);

/* Do not use this function for normal sieve extensions. This is intended for
 * the testsuite only.
 */
void sieve_interpreter_set_result
	(struct sieve_interpreter *interp, struct sieve_result *result);

/*
 * Program flow
 */

void sieve_interpreter_reset
	(struct sieve_interpreter *interp);
void sieve_interpreter_interrupt
	(struct sieve_interpreter *interp);
sieve_size_t sieve_interpreter_program_counter
	(struct sieve_interpreter *interp);

int sieve_interpreter_program_jump
	(struct sieve_interpreter *interp, bool jump);
	
/*
 * Test results
 */	
	
void sieve_interpreter_set_test_result
	(struct sieve_interpreter *interp, bool result);
bool sieve_interpreter_get_test_result
	(struct sieve_interpreter *interp);
	
/* 
 * Error handling 
 */

/* This is not particularly user-friendly, so avoid using this.. */
const char *sieve_runtime_location(const struct sieve_runtime_env *runenv);

void sieve_runtime_error
	(const struct sieve_runtime_env *runenv, const char *location,
		const char *fmt, ...) ATTR_FORMAT(3, 4);
void sieve_runtime_warning
	(const struct sieve_runtime_env *runenv, const char *location,
		const char *fmt, ...) ATTR_FORMAT(3, 4);
void sieve_runtime_log
	(const struct sieve_runtime_env *runenv, const char *location, 
		const char *fmt, ...) ATTR_FORMAT(3, 4);

/* 
 * Runtime Trace 
 */

#ifdef SIEVE_RUNTIME_TRACE
		
void _sieve_runtime_trace
	(const struct sieve_runtime_env *runenv, const char *fmt, ...)
		ATTR_FORMAT(2, 3);
void _sieve_runtime_trace_error
	(const struct sieve_runtime_env *runenv, const char *fmt, ...)
		ATTR_FORMAT(2, 3);
		
# define sieve_runtime_trace(runenv, ...) STMT_START { \
		if ( (runenv)->trace_stream != NULL ) \
			_sieve_runtime_trace((runenv), __VA_ARGS__); \
	} STMT_END
# define sieve_runtime_trace_error(runenv, ...) STMT_START { \
		if ( (runenv)->trace_stream != NULL ) \
			_sieve_runtime_trace_error((runenv), __VA_ARGS__); \
		} STMT_END	

#else
# define sieve_runtime_trace(runenv, ...)
# define sieve_runtime_trace_error(runenv, ...)
#endif

/* 
 * Extension support 
 */

struct sieve_interpreter_extension {
	const struct sieve_extension_def *ext_def;	

	void (*run)
		(const struct sieve_extension *ext, const struct sieve_runtime_env *renv, 
			void *context);
	void (*free)
		(const struct sieve_extension *ext, struct sieve_interpreter *interp, 
			void *context);
};

void sieve_interpreter_extension_register
	(struct sieve_interpreter *interp, const struct sieve_extension *ext,
		const struct sieve_interpreter_extension *intext, void *context);
void sieve_interpreter_extension_set_context
	(struct sieve_interpreter *interp, const struct sieve_extension *ext, 
		void *context);
void *sieve_interpreter_extension_get_context
	(struct sieve_interpreter *interp, const struct sieve_extension *ext); 

/* 
 * Opcodes and operands 
 */
	
int sieve_interpreter_handle_optional_operands
	(const struct sieve_runtime_env *renv, sieve_size_t *address,
		struct sieve_side_effects_list **list);

/* 
 * Code execute 
 */

int sieve_interpreter_continue
	(struct sieve_interpreter *interp, bool *interrupted);
int sieve_interpreter_start
	(struct sieve_interpreter *interp, const struct sieve_message_data *msgdata,
		const struct sieve_script_env *senv, struct sieve_result *result, 
		bool *interrupted);
int sieve_interpreter_run
	(struct sieve_interpreter *interp, const struct sieve_message_data *msgdata,
		const struct sieve_script_env *senv, struct sieve_result *result);

#endif /* __SIEVE_INTERPRETER_H */
