#include <stdio.h>
#include <string.h>

#include "lib.h"
#include "ostream.h"
#include "mempool.h"
#include "array.h"
#include "hash.h"
#include "mail-storage.h"

#include "sieve-error.h"
#include "sieve-extensions.h"
#include "sieve-message.h"
#include "sieve-commands-private.h"
#include "sieve-code.h"
#include "sieve-actions.h"
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-result.h"
#include "sieve-comparators.h"

#include "sieve-interpreter.h"

/* 
 * Interpreter 
 */

struct sieve_interpreter {
	pool_t pool;
			
	struct sieve_error_handler *ehandler;

	/* Runtime data for extensions */
	ARRAY_DEFINE(ext_contexts, void *); 
		
	/* Execution status */
	
	sieve_size_t pc;          /* Program counter */
	bool interrupted;         /* Interpreter interrupt requested */
	bool test_result;         /* Result of previous test command */

	const struct sieve_operation *current_op; /* Current operation */ 
	sieve_size_t current_op_addr;             /* Start address of current operation */
	
	/* Runtime environment environment */
	struct sieve_runtime_env runenv; 
};

struct sieve_interpreter *sieve_interpreter_create
(struct sieve_binary *sbin, struct sieve_error_handler *ehandler,
	struct ostream *trace_stream) 
{
	unsigned int i;
	int idx;

	pool_t pool;
	struct sieve_interpreter *interp;
	
	pool = pool_alloconly_create("sieve_interpreter", 4096);	
	interp = p_new(pool, struct sieve_interpreter, 1);
	interp->pool = pool;

	interp->ehandler = ehandler;
	sieve_error_handler_ref(ehandler);

	interp->runenv.interp = interp;	
	interp->runenv.sbin = sbin;
	interp->runenv.trace_stream = trace_stream;
	interp->runenv.script = sieve_binary_script(sbin);
	sieve_binary_ref(sbin);
	
	
	interp->pc = 0;

	p_array_init(&interp->ext_contexts, pool, 4);

	/* Pre-load core language features implemented as 'extensions' */
	for ( i = 0; i < sieve_preloaded_extensions_count; i++ ) {
		const struct sieve_extension *ext = sieve_preloaded_extensions[i];
		
		if ( ext->interpreter_load != NULL )
			(void)ext->interpreter_load(interp);		
	}

	/* Load other extensions listed in the binary */
	for ( idx = 0; idx < sieve_binary_extensions_count(sbin); idx++ ) {
		const struct sieve_extension *ext = 
			sieve_binary_extension_get_by_index(sbin, idx, NULL);
		
		if ( ext->interpreter_load != NULL )
			ext->interpreter_load(interp);
	}
	
	return interp;
}

void sieve_interpreter_free(struct sieve_interpreter **interp) 
{
	sieve_binary_unref(&(*interp)->runenv.sbin);

	if ( (*interp)->runenv.msgctx != NULL )
		 sieve_message_context_unref(&(*interp)->runenv.msgctx);

	sieve_error_handler_unref(&(*interp)->ehandler);
		 
	pool_unref(&((*interp)->pool));
	
	*interp = NULL;
}

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

/* Error handling */

/* This is not particularly user friendly, so we might want to consider storing
 * the original line numbers of the script in the binary somewhere...
 */
static const char *_get_location(const struct sieve_runtime_env *runenv)
{
	const char *op = runenv->interp->current_op == NULL ?
		"<<NOOP>>" : runenv->interp->current_op->mnemonic;
	return t_strdup_printf("#%08x: %s", runenv->interp->current_op_addr, op);
}

void sieve_runtime_error
	(const struct sieve_runtime_env *runenv, const char *fmt, ...)
{
	va_list args;
	
	va_start(args, fmt);
	T_BEGIN {
		sieve_verror(runenv->interp->ehandler, _get_location(runenv), fmt, args); 
	} T_END;
	va_end(args);
}

void sieve_runtime_warning
	(const struct sieve_runtime_env *runenv, const char *fmt, ...)
{	
	va_list args;
	
	va_start(args, fmt);
	T_BEGIN {
		sieve_vwarning(runenv->interp->ehandler, _get_location(runenv), fmt, args);
	} T_END; 
	va_end(args);
}

void sieve_runtime_log
	(const struct sieve_runtime_env *runenv, const char *fmt, ...)
{	
	va_list args;
	
	va_start(args, fmt);
	T_BEGIN {
		sieve_vinfo(runenv->interp->ehandler, _get_location(runenv), fmt, args); 
	} T_END;
	va_end(args);
}

#ifdef SIEVE_RUNTIME_TRACE
void _sieve_runtime_trace
	(const struct sieve_runtime_env *runenv, const char *fmt, ...)
{	
	string_t *outbuf = t_str_new(128);
	va_list args;
	
	va_start(args, fmt);	
	str_printfa(outbuf, "%08x: ", runenv->interp->current_op_addr); 
	T_BEGIN {
		str_vprintfa(outbuf, fmt, args); 
	} T_END;
	str_append_c(outbuf, '\n');

	va_end(args);
	
	o_stream_send(runenv->trace_stream, str_data(outbuf), str_len(outbuf));	
}
#endif

/* Extension support */

void sieve_interpreter_extension_set_context
	(struct sieve_interpreter *interpreter, int ext_id, void *context)
{
	array_idx_set(&interpreter->ext_contexts, (unsigned int) ext_id, &context);	
}

const void *sieve_interpreter_extension_get_context
	(struct sieve_interpreter *interpreter, int ext_id) 
{
	void * const *ctx;

	if  ( ext_id < 0 || ext_id >= (int) array_count(&interpreter->ext_contexts) )
		return NULL;
	
	ctx = array_idx(&interpreter->ext_contexts, (unsigned int) ext_id);		

	return *ctx;
}

/* Program counter */

void sieve_interpreter_reset(struct sieve_interpreter *interp) 
{
	interp->pc = 0;
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

bool sieve_interpreter_program_jump
	(struct sieve_interpreter *interp, bool jump)
{
	sieve_size_t pc = interp->pc;
	int offset;
	
	if ( !sieve_binary_read_offset(interp->runenv.sbin, &(interp->pc), &offset) )
		return FALSE;

	if ( pc + offset <= sieve_binary_get_code_size(interp->runenv.sbin) && 
		pc + offset > 0 ) 
	{	
		if ( jump )
			interp->pc = pc + offset;
		
		return TRUE;
	}
	
	return FALSE;
}

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

/* Operations and operands */

bool sieve_interpreter_handle_optional_operands
	(const struct sieve_runtime_env *renv, sieve_size_t *address,
		struct sieve_side_effects_list **list)
{
	int opt_code = -1;
	
	if ( sieve_operand_optional_present(renv->sbin, address) ) {
		while ( opt_code != 0 ) {
			if ( !sieve_operand_optional_read(renv->sbin, address, &opt_code) )
				return FALSE;

			if ( opt_code == SIEVE_OPT_SIDE_EFFECT ) {
				void *context = NULL;
			
				if ( list != NULL && *list == NULL ) 
					*list = sieve_side_effects_list_create(renv->result);
					
				const struct sieve_side_effect *seffect = 
					sieve_opr_side_effect_read(renv, address);

				if ( seffect == NULL ) return FALSE;
			
				if ( list != NULL ) {
					if ( seffect->read_context != NULL && !seffect->read_context
						(seffect, renv, address, &context) )
						return FALSE;
				
					sieve_side_effects_list_add(*list, seffect, context);
				}
			}
		}
	}
	return TRUE;
}
 
/* Code execute */

bool sieve_interpreter_execute_operation
	(struct sieve_interpreter *interp) 
{
	const struct sieve_operation *op;

	interp->current_op_addr = interp->pc;
	interp->current_op = op =
		sieve_operation_read(interp->runenv.sbin, &(interp->pc));

	if ( op != NULL ) {
		if ( op->execute != NULL )
			return op->execute(op, &(interp->runenv), &(interp->pc));
		else
			return FALSE;
			
		return TRUE;
	}
	
	return FALSE;
}		

int sieve_interpreter_continue
(struct sieve_interpreter *interp, bool *interrupted) 
{
	int ret = 1;
	
	sieve_result_ref(interp->runenv.result);
	interp->interrupted = FALSE;
	
	if ( interrupted != NULL )
		*interrupted = FALSE;
	
	while ( ret >= 0 && !interp->interrupted && 
		interp->pc < sieve_binary_get_code_size(interp->runenv.sbin) ) {
		
		if ( !sieve_interpreter_execute_operation(interp) ) {
			sieve_runtime_trace(&interp->runenv, "[[EXECUTION ABORTED]]");
			ret = -1;
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
	struct sieve_binary *sbin = interp->runenv.sbin;
	unsigned int i;
	int idx;
	
	interp->runenv.msgdata = msgdata;
	interp->runenv.result = result;		
	interp->runenv.scriptenv = senv;
	
	if ( msgctx == NULL )
		interp->runenv.msgctx = sieve_message_context_create();
	else {
		interp->runenv.msgctx = msgctx;
		sieve_message_context_ref(msgctx);
	}
	
	/* Pre-load core language features implemented as 'extensions' */
	for ( i = 0; i < sieve_preloaded_extensions_count; i++ ) {
		const struct sieve_extension *ext = sieve_preloaded_extensions[i];
		
		if ( ext->runtime_load != NULL )
			(void)ext->runtime_load(&interp->runenv);		
	}

	/* Load other extensions listed in the binary */
	for ( idx = 0; idx < sieve_binary_extensions_count(sbin); idx++ ) {
		const struct sieve_extension *ext = 
			sieve_binary_extension_get_by_index(sbin, idx, NULL);
		
		if ( ext->runtime_load != NULL )
			ext->runtime_load(&interp->runenv);
	}

	return sieve_interpreter_continue(interp, interrupted); 
}

int sieve_interpreter_run
(struct sieve_interpreter *interp, const struct sieve_message_data *msgdata,
	const struct sieve_script_env *senv, struct sieve_result **result) 
{
	bool is_topmost = ( *result == NULL );
	int ret = 0;
	sieve_interpreter_reset(interp);
	
	if ( is_topmost )
		*result = sieve_result_create(interp->ehandler);
	else {
		sieve_result_ref(*result);
	}
	
	ret = sieve_interpreter_start(interp, msgdata, senv, NULL, *result, NULL);

	if ( ret >= 0 && is_topmost ) {
		ret = sieve_result_execute(*result, msgdata, senv);
	}
	
	sieve_result_unref(result);
	
	return ret;
}


