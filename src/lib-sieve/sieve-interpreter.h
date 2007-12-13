#ifndef __SIEVE_INTERPRETER_H
#define __SIEVE_INTERPRETER_H

#include "lib.h"
#include "array.h"
#include "buffer.h"
#include "mail-storage.h"

#include "sieve-common.h"

/* FIXME: make dumps use some interpreter function */
#include <stdio.h>

struct sieve_interpreter;

struct sieve_runtime_env {
	struct sieve_interpreter *interp;
	struct sieve_binary *sbin;
	const struct sieve_message_data *msgdata;
	const struct sieve_script_env *scriptenv;
	struct sieve_result *result;
};

struct sieve_interpreter *sieve_interpreter_create
	(struct sieve_binary *sbin, struct sieve_error_handler *ehandler);
void sieve_interpreter_free(struct sieve_interpreter *interp);
inline pool_t sieve_interpreter_pool(struct sieve_interpreter *interp);

inline void sieve_interpreter_reset
	(struct sieve_interpreter *interp);
inline void sieve_interpreter_interrupt
	(struct sieve_interpreter *interp);
inline sieve_size_t sieve_interpreter_program_counter
	(struct sieve_interpreter *interp);
inline bool sieve_interpreter_program_jump
	(struct sieve_interpreter *interp, bool jump);
	
inline void sieve_interpreter_set_test_result
	(struct sieve_interpreter *interp, bool result);
inline bool sieve_interpreter_get_test_result
	(struct sieve_interpreter *interp);
	
/* Error handling */

void sieve_runtime_error
	(const struct sieve_runtime_env *runenv, const char *fmt, ...)
		ATTR_FORMAT(2, 3);
void sieve_runtime_warning
	(const struct sieve_runtime_env *runenv, const char *fmt, ...)
		ATTR_FORMAT(2, 3);
void sieve_runtime_log
	(const struct sieve_runtime_env *runenv, const char *fmt, ...)
		ATTR_FORMAT(2, 3);

/* Extension support */

inline void sieve_interpreter_extension_set_context
	(struct sieve_interpreter *interp, int ext_id, void *context);
inline const void *sieve_interpreter_extension_get_context
	(struct sieve_interpreter *interp, int ext_id);
	
/* Opcodes and operands */
	
bool sieve_interpreter_handle_optional_operands
	(const struct sieve_runtime_env *renv, sieve_size_t *address,
		struct sieve_side_effects_list **list);

/* Code execute */

bool sieve_interpreter_execute_operation(struct sieve_interpreter *interp); 

int sieve_interpreter_continue
	(struct sieve_interpreter *interp, bool *interrupted);
int sieve_interpreter_start
	(struct sieve_interpreter *interp, const struct sieve_message_data *msgdata,
		const struct sieve_script_env *senv, struct sieve_result **result,
		bool *interrupted);
int sieve_interpreter_run
	(struct sieve_interpreter *interp, const struct sieve_message_data *msgdata,
		const struct sieve_script_env *senv, struct sieve_result **result);

#endif /* __SIEVE_INTERPRETER_H */
