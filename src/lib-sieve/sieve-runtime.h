#ifndef SIEVE_RUNTIME_H
#define SIEVE_RUNTIME_H

#include "sieve-common.h"
#include "sieve-execute.h"

/*
 * Runtime environment
 */

struct sieve_runtime_env {
	const struct sieve_execute_env *exec_env;
	struct event *event;

	/* Interpreter */
	struct sieve_interpreter *interp;
	struct sieve_error_handler *ehandler;

	/* Executing script */
	struct sieve_script *script;

	/* Executing binary */
	struct sieve_binary *sbin;
	struct sieve_binary_block *sblock;

	/* Current code */
	sieve_size_t pc;
	const struct sieve_operation *oprtn;

	/* Tested message */
	struct sieve_message_context *msgctx;

	/* Filter result */
	struct sieve_result *result;

	/* Runtime tracing */
	struct sieve_runtime_trace *trace;
};

#endif
