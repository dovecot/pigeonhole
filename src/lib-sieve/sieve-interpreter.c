/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "ostream.h"
#include "mempool.h"
#include "array.h"
#include "hash.h"
#include "mail-storage.h"

#include "sieve-common.h"
#include "sieve-script.h"
#include "sieve-error.h"
#include "sieve-extensions.h"
#include "sieve-message.h"
#include "sieve-commands.h"
#include "sieve-code.h"
#include "sieve-actions.h"
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-result.h"
#include "sieve-comparators.h"

#include "sieve-interpreter.h"

#include <string.h>

/* 
 * Interpreter extension 
 */

struct sieve_interpreter_extension_reg {
	const struct sieve_interpreter_extension *int_ext;
	void *context;
};

/* 
 * Interpreter 
 */

struct sieve_interpreter {
	pool_t pool;
			
	struct sieve_error_handler *ehandler;

	/* Runtime data for extensions */
	ARRAY_DEFINE(extensions, struct sieve_interpreter_extension_reg); 
	
	sieve_size_t reset_vector;	
		
	/* Execution status */
	
	sieve_size_t pc;          /* Program counter */
	bool interrupted;         /* Interpreter interrupt requested */
	bool test_result;         /* Result of previous test command */

	/* Current operation */ 
	const struct sieve_operation *current_op;
	
	/* Start address of current operation */
	sieve_size_t current_op_addr;             
	
	/* Runtime environment */
	struct sieve_runtime_env runenv; 
};

struct sieve_interpreter *sieve_interpreter_create
(struct sieve_binary *sbin, struct sieve_error_handler *ehandler) 
{
	unsigned int i, ext_count;
	bool success = TRUE;

	pool_t pool;
	struct sieve_interpreter *interp;
	
	pool = pool_alloconly_create("sieve_interpreter", 4096);	
	interp = p_new(pool, struct sieve_interpreter, 1);
	interp->pool = pool;

	interp->ehandler = ehandler;
	sieve_error_handler_ref(ehandler);

	interp->runenv.interp = interp;	
	interp->runenv.sbin = sbin;
	interp->runenv.script = sieve_binary_script(sbin);
	sieve_binary_ref(sbin);
	
	interp->pc = 0;

	p_array_init(&interp->extensions, pool, sieve_extensions_get_count());

	/* Pre-load core language features implemented as 'extensions' */
	for ( i = 0; i < sieve_preloaded_extensions_count; i++ ) {
		const struct sieve_extension *ext = sieve_preloaded_extensions[i];
		
		if ( ext->interpreter_load != NULL )
			(void)ext->interpreter_load(&interp->runenv, &interp->pc);		
	}

	/* Load other extensions listed in code */
	if ( sieve_binary_read_unsigned(sbin, &interp->pc, &ext_count) ) {
		for ( i = 0; i < ext_count; i++ ) {
			unsigned int code = 0;
			const struct sieve_extension *ext;
			
			if ( !sieve_binary_read_extension(sbin, &interp->pc, &code, &ext) ) {
				success = FALSE;
				break;
			}
 
			if ( ext->interpreter_load != NULL && 
				!ext->interpreter_load(&interp->runenv, &interp->pc) ) {
				success = FALSE;
				break;
			}
		}
	}	else
		success = FALSE;
	
	if ( !success ) {
		sieve_interpreter_free(&interp);
	} else {
		interp->reset_vector = interp->pc;
	}
	
	return interp;
}

void sieve_interpreter_free(struct sieve_interpreter **interp) 
{
	const struct sieve_interpreter_extension_reg *extrs;
	unsigned int ext_count, i;

	sieve_binary_unref(&(*interp)->runenv.sbin);

	if ( (*interp)->runenv.msgctx != NULL )
		 sieve_message_context_unref(&(*interp)->runenv.msgctx);

	sieve_error_handler_unref(&(*interp)->ehandler);

	/* Signal registered extensions that the interpreter is being destroyed */
	extrs = array_get(&(*interp)->extensions, &ext_count);
	for ( i = 0; i < ext_count; i++ ) {
		if ( extrs[i].int_ext != NULL && extrs[i].int_ext->free != NULL )
			extrs[i].int_ext->free(*interp, extrs[i].context);
	}
		 
	pool_unref(&((*interp)->pool));	
	*interp = NULL;
}

/*
 * Accessors
 */

pool_t sieve_interpreter_pool(struct sieve_interpreter *interp)
{
	return interp->pool;
}

struct sieve_script *sieve_interpreter_script
(struct sieve_interpreter *interp)
{
	return interp->runenv.script;
}

struct sieve_error_handler *sieve_interpreter_get_error_handler
(struct sieve_interpreter *interp)
{
	return interp->ehandler;
}

/* Do not use this function for normal sieve extensions. This is intended for
 * the testsuite only.
 */
void sieve_interpreter_set_result
(struct sieve_interpreter *interp, struct sieve_result *result)
{
	sieve_result_unref(&interp->runenv.result);
	interp->runenv.result = result;
	sieve_result_ref(result);
}

/* 
 * Error handling 
 */

/* This is not particularly user friendly, so avoid using this
 */
const char *sieve_runtime_location(const struct sieve_runtime_env *runenv)
{
	const char *op = runenv->interp->current_op == NULL ?
		"<<NOOP>>" : runenv->interp->current_op->mnemonic;
	return t_strdup_printf("%s: #%08llx: %s", sieve_script_name(runenv->script),
		(unsigned long long) runenv->interp->current_op_addr, op);
}

void sieve_runtime_error
(const struct sieve_runtime_env *runenv, const char *location,
	const char *fmt, ...)
{
	va_list args;
	
	va_start(args, fmt);
	T_BEGIN {
		sieve_verror(runenv->interp->ehandler, location, fmt, args); 
	} T_END;
	va_end(args);
}

void sieve_runtime_warning
(const struct sieve_runtime_env *runenv, const char *location,
	const char *fmt, ...)
{	
	va_list args;
	
	va_start(args, fmt);
	T_BEGIN {
		sieve_vwarning(runenv->interp->ehandler, location, fmt, args);
	} T_END; 
	va_end(args);
}

void sieve_runtime_log
(const struct sieve_runtime_env *runenv, const char *location,
	const char *fmt, ...)
{	
	va_list args;
	
	va_start(args, fmt);
	T_BEGIN {
		sieve_vinfo(runenv->interp->ehandler, location, fmt, args); 
	} T_END;
	va_end(args);
}

/*
 * Runtime trace
 */

#ifdef SIEVE_RUNTIME_TRACE
void _sieve_runtime_trace
(const struct sieve_runtime_env *runenv, const char *fmt, ...)
{	
	string_t *outbuf = t_str_new(128);
	va_list args;
	
	va_start(args, fmt);	
	str_printfa(outbuf, "%08llx: ", (unsigned long long) runenv->interp->current_op_addr); 
	str_vprintfa(outbuf, fmt, args); 
	str_append_c(outbuf, '\n');
	va_end(args);
	
	o_stream_send(runenv->trace_stream, str_data(outbuf), str_len(outbuf));	
}

void _sieve_runtime_trace_error
(const struct sieve_runtime_env *runenv, const char *fmt, ...)
{
	string_t *outbuf = t_str_new(128);
	va_list args;

	va_start(args, fmt);
	str_printfa(outbuf, "%08llx: [[ERROR: %s: ", 
		(unsigned long long) runenv->interp->pc, 
		runenv->interp->current_op->mnemonic);
	str_vprintfa(outbuf, fmt, args);
    str_append(outbuf, "]]\n");
	va_end(args);

	o_stream_send(runenv->trace_stream, str_data(outbuf), str_len(outbuf));
}
#endif

/* 
 * Extension support 
 */

void sieve_interpreter_extension_register
(struct sieve_interpreter *interp, 
	const struct sieve_interpreter_extension *int_ext, void *context)
{
	struct sieve_interpreter_extension_reg reg = { int_ext, context };
	int ext_id = SIEVE_EXT_ID(int_ext->ext);

	if ( ext_id < 0 ) return;
	
	array_idx_set(&interp->extensions, (unsigned int) ext_id, &reg);	
}

void sieve_interpreter_extension_set_context
(struct sieve_interpreter *interp, const struct sieve_extension *ext, 
	void *context)
{
	struct sieve_interpreter_extension_reg reg = { NULL, context };
	int ext_id = SIEVE_EXT_ID(ext);

	if ( ext_id < 0 ) return;
	
	array_idx_set(&interp->extensions, (unsigned int) ext_id, &reg);	
}

void *sieve_interpreter_extension_get_context
(struct sieve_interpreter *interp, const struct sieve_extension *ext) 
{
	int ext_id = SIEVE_EXT_ID(ext);
	const struct sieve_interpreter_extension_reg *reg;

	if  ( ext_id < 0 || ext_id >= (int) array_count(&interp->extensions) )
		return NULL;
	
	reg = array_idx(&interp->extensions, (unsigned int) ext_id);		

	return reg->context;
}

/* 
 * Program flow 
 */

void sieve_interpreter_reset(struct sieve_interpreter *interp) 
{
	interp->pc = interp->reset_vector;
	interp->interrupted = FALSE;
	interp->test_result = FALSE;
	interp->runenv.msgdata = NULL;
	interp->runenv.result = NULL;
}

void sieve_interpreter_interrupt(struct sieve_interpreter *interp)
{
	interp->interrupted = TRUE;
}

sieve_size_t sieve_interpreter_program_counter(struct sieve_interpreter *interp)
{
	return interp->pc;
}

int sieve_interpreter_program_jump
(struct sieve_interpreter *interp, bool jump)
{
	const struct sieve_runtime_env *renv = &interp->runenv;
	sieve_size_t pc = interp->pc;
	int offset;
	
	if ( !sieve_binary_read_offset(renv->sbin, &(interp->pc), &offset) )
	{
		sieve_runtime_trace_error(renv, "invalid jump offset"); 
		return SIEVE_EXEC_BIN_CORRUPT;
	}

	if ( pc + offset <= sieve_binary_get_code_size(renv->sbin) && 
		pc + offset > 0 ) 
	{	
		if ( jump )
			interp->pc = pc + offset;
		
		return SIEVE_EXEC_OK;
	}
	
	sieve_runtime_trace_error(renv, "jump offset out of range");
	return SIEVE_EXEC_BIN_CORRUPT;
}

/*
 * Test results
 */

void sieve_interpreter_set_test_result
(struct sieve_interpreter *interp, bool result)
{
	interp->test_result = result;
}

bool sieve_interpreter_get_test_result
(struct sieve_interpreter *interp)
{
	return interp->test_result;
}

/* 
 * Operations and operands 
 */

int sieve_interpreter_handle_optional_operands
(const struct sieve_runtime_env *renv, sieve_size_t *address,
	struct sieve_side_effects_list **list)
{
	int opt_code = -1;
	
	if ( sieve_operand_optional_present(renv->sbin, address) ) {
		while ( opt_code != 0 ) {
			if ( !sieve_operand_optional_read(renv->sbin, address, &opt_code) ) {
				sieve_runtime_trace_error(renv, "invalid optional operand");
				return SIEVE_EXEC_BIN_CORRUPT;
			}

			if ( opt_code == SIEVE_OPT_SIDE_EFFECT ) {
				void *context = NULL;
			
				if ( list != NULL && *list == NULL ) 
					*list = sieve_side_effects_list_create(renv->result);
					
				const struct sieve_side_effect *seffect = 
					sieve_opr_side_effect_read(renv, address);

				if ( seffect == NULL ) {
					sieve_runtime_trace_error(renv, "invalid side effect operand");
					return SIEVE_EXEC_BIN_CORRUPT;
				}
			
				if ( list != NULL ) {
					if ( seffect->read_context != NULL && !seffect->read_context
						(seffect, renv, address, &context) ) {
						sieve_runtime_trace_error(renv, "invalid side effect context");
						return SIEVE_EXEC_BIN_CORRUPT;
					}
				
					sieve_side_effects_list_add(*list, seffect, context);
				}
			}
		}
	}
	return TRUE;
}
 
/* 
 * Code execute 
 */

static int sieve_interpreter_execute_operation
(struct sieve_interpreter *interp) 
{
	const struct sieve_operation *op;

	interp->current_op_addr = interp->pc;
	interp->current_op = op =
		sieve_operation_read(interp->runenv.sbin, &(interp->pc));

	if ( op != NULL ) {
		int result = SIEVE_EXEC_OK;

		if ( op->execute != NULL ) { /* Noop ? */
			T_BEGIN {
				result = op->execute(op, &(interp->runenv), &(interp->pc));
			} T_END;
		} else {
			sieve_runtime_trace(&interp->runenv, "OP: %s (NOOP)", op->mnemonic);
		}

		return result;
	}
	
	sieve_runtime_trace(&interp->runenv, "Encountered invalid operation");	
	return SIEVE_EXEC_BIN_CORRUPT;
}		

int sieve_interpreter_continue
(struct sieve_interpreter *interp, bool *interrupted) 
{
	int ret = SIEVE_EXEC_OK;
	
	sieve_result_ref(interp->runenv.result);
	interp->interrupted = FALSE;
	
	if ( interrupted != NULL )
		*interrupted = FALSE;
	
	while ( ret == SIEVE_EXEC_OK && !interp->interrupted && 
		interp->pc < sieve_binary_get_code_size(interp->runenv.sbin) ) {
		
		ret = sieve_interpreter_execute_operation(interp);

		if ( ret != SIEVE_EXEC_OK ) {
			sieve_runtime_trace(&interp->runenv, "[[EXECUTION ABORTED]]");
		}
	}
	
	if ( interrupted != NULL )
		*interrupted = interp->interrupted;
			
	sieve_result_unref(&interp->runenv.result);
	return ret;
}

int sieve_interpreter_start
(struct sieve_interpreter *interp, const struct sieve_message_data *msgdata,
	const struct sieve_script_env *senv, struct sieve_message_context *msgctx, 
	struct sieve_result *result, bool *interrupted) 
{
	const struct sieve_interpreter_extension_reg *extrs;
	unsigned int ext_count, i;
	
	interp->runenv.msgdata = msgdata;
	interp->runenv.result = result;		
	interp->runenv.scriptenv = senv;
	interp->runenv.trace_stream = senv->trace_stream;

	if ( senv->exec_status == NULL ) 
		interp->runenv.exec_status = p_new(interp->pool, struct sieve_exec_status, 1);
	else
		interp->runenv.exec_status = senv->exec_status;
	
	if ( msgctx == NULL )
		interp->runenv.msgctx = sieve_message_context_create();
	else {
		interp->runenv.msgctx = msgctx;
		sieve_message_context_ref(msgctx);
	}

	/* Signal registered extensions that the interpreter is being run */
	extrs = array_get(&interp->extensions, &ext_count);
	for ( i = 0; i < ext_count; i++ ) {
		if ( extrs[i].int_ext != NULL && extrs[i].int_ext->run != NULL )
			extrs[i].int_ext->run(&interp->runenv, extrs[i].context);
	}

	return sieve_interpreter_continue(interp, interrupted); 
}

int sieve_interpreter_run
(struct sieve_interpreter *interp, const struct sieve_message_data *msgdata,
	const struct sieve_script_env *senv, struct sieve_result *result)
{
	int ret = 0;
	
	sieve_interpreter_reset(interp);
	sieve_result_ref(result);
	
	ret = sieve_interpreter_start(interp, msgdata, senv, NULL, result, NULL);
	
	sieve_result_unref(&result);
	
	return ret;
}


