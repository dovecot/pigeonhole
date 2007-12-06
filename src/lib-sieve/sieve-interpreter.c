#include <stdio.h>
#include <string.h>

#include "lib.h"
#include "mempool.h"
#include "array.h"
#include "hash.h"
#include "mail-storage.h"

#include "sieve-error.h"
#include "sieve-extensions.h"
#include "sieve-commands-private.h"
#include "sieve-actions.h"
#include "sieve-generator.h"
#include "sieve-binary.h"
#include "sieve-result.h"
#include "sieve-comparators.h"

#include "sieve-interpreter.h"

struct sieve_interpreter {
	pool_t pool;
			
	struct sieve_error_handler *ehandler;

	/* Runtime data for extensions */
	ARRAY_DEFINE(ext_contexts, void *); 
		
	/* Execution status */
	
	sieve_size_t pc;          /* Program counter */
	bool stopped;             /* Explicit successful stop requested */
	bool test_result;         /* Result of previous test command */

	const struct sieve_opcode *current_op; /* Current opcode */ 
	sieve_size_t current_op_addr;          /* Start address of current opcode */
	
	/* Runtime environment environment */
	struct sieve_runtime_env runenv; 
};

struct sieve_interpreter *sieve_interpreter_create
(struct sieve_binary *sbin, struct sieve_error_handler *ehandler) 
{
	unsigned int i;
	int idx;
	pool_t pool;
	struct sieve_interpreter *interp;
	
	pool = pool_alloconly_create("sieve_interpreter", 4096);	
	interp = p_new(pool, struct sieve_interpreter, 1);
	interp->pool = pool;
	interp->ehandler = ehandler;
	interp->runenv.interp = interp;
	
	interp->runenv.sbin = sbin;
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

void sieve_interpreter_free(struct sieve_interpreter *interp) 
{
	sieve_binary_unref(&interp->runenv.sbin);
	pool_unref(&(interp->pool));
}

inline pool_t sieve_interpreter_pool(struct sieve_interpreter *interp)
{
	return interp->pool;
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
	T_FRAME(
		sieve_verror(runenv->interp->ehandler, _get_location(runenv), fmt, args); 
	);
	va_end(args);
}

void sieve_runtime_warning
	(const struct sieve_runtime_env *runenv, const char *fmt, ...)
{	
	va_list args;
	
	va_start(args, fmt);
	T_FRAME(
		sieve_vwarning(runenv->interp->ehandler, _get_location(runenv), fmt, args);
	); 
	va_end(args);
}

void sieve_runtime_log
	(const struct sieve_runtime_env *runenv, const char *fmt, ...)
{	
	va_list args;
	
	va_start(args, fmt);
	T_FRAME(
		sieve_vinfo(runenv->interp->ehandler, _get_location(runenv), fmt, args); 
	);
	va_end(args);
}

/* Extension support */

inline void sieve_interpreter_extension_set_context
	(struct sieve_interpreter *interpreter, int ext_id, void *context)
{
	array_idx_set(&interpreter->ext_contexts, (unsigned int) ext_id, &context);	
}

inline const void *sieve_interpreter_extension_get_context
	(struct sieve_interpreter *interpreter, int ext_id) 
{
	void * const *ctx;

	if  ( ext_id < 0 || ext_id >= (int) array_count(&interpreter->ext_contexts) )
		return NULL;
	
	ctx = array_idx(&interpreter->ext_contexts, (unsigned int) ext_id);		

	return *ctx;
}

/* Program counter */

inline void sieve_interpreter_reset(struct sieve_interpreter *interp) 
{
	interp->pc = 0;
	interp->stopped = FALSE;
	interp->test_result = FALSE;
	interp->runenv.msgdata = NULL;
	interp->runenv.result = NULL;
}

inline void sieve_interpreter_stop(struct sieve_interpreter *interp)
{
	interp->stopped = TRUE;
}

inline sieve_size_t sieve_interpreter_program_counter(struct sieve_interpreter *interp)
{
	return interp->pc;
}

inline bool sieve_interpreter_program_jump
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

inline void sieve_interpreter_set_test_result
	(struct sieve_interpreter *interp, bool result)
{
	interp->test_result = result;
}

inline bool sieve_interpreter_get_test_result
	(struct sieve_interpreter *interp)
{
	return interp->test_result;
}

/* Opcodes and operands */

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
					sieve_opr_side_effect_read(renv->sbin, address);

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
	const struct sieve_opcode *opcode;

	interp->current_op_addr = interp->pc;
	interp->current_op = opcode =
		sieve_operation_read(interp->runenv.sbin, &(interp->pc));

	if ( opcode != NULL ) {
		if ( opcode->execute != NULL )
			return opcode->execute(opcode, &(interp->runenv), &(interp->pc));
		else
			return FALSE;
			
		return TRUE;
	}
	
	return FALSE;
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
	interp->runenv.msgdata = msgdata;
	interp->runenv.result = *result;		
	interp->runenv.scriptenv = senv;
	
	while ( !interp->stopped && 
		interp->pc < sieve_binary_get_code_size(interp->runenv.sbin) ) {
		printf("%08x: ", interp->pc);
		
		if ( !sieve_interpreter_execute_operation(interp) ) {
			printf("Execution aborted.\n");
			sieve_result_unref(result);
			return -1;
		}
	}
	
	interp->runenv.result = NULL;
	interp->runenv.msgdata = NULL;
	interp->runenv.scriptenv = NULL;
	
	ret = 1;
	if ( is_topmost ) {
		ret = sieve_result_execute(*result, msgdata, senv);
	}
	
	sieve_result_unref(result);
	
	return ret;
}


