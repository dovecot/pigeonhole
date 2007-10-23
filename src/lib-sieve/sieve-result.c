#include "lib.h"
#include "mempool.h"

#include "sieve-result.h"

struct sieve_result_action {
	struct sieve_action *action;
	void *context;

	struct sieve_result_action *prev, *next; 
};

struct sieve_result {
	pool_t pool;
	
	struct sieve_result_action *first_action;
	struct sieve_result_action *last_action;
	
	unsigned int implicit_keep:1; 
};

struct sieve_result *sieve_result_create() 
{
	pool_t pool;
	struct sieve_result *result;
	
	pool = pool_alloconly_create("sieve_result", 4096);	
	result = p_new(pool, struct sieve_result, 1);
	result->pool = pool;
	
	result->first_action = NULL;
	result->last_action = NULL;
	
	result->implicit_keep = 1;

	return result;
}

void sieve_result_free(struct sieve_result *result) 
{
	pool_unref(result->pool);
}

void sieve_result_add_action
	(struct sieve_result *result, struct sieve_action *action, void *context)		
{
	struct sieve_result_action *raction;
	
	/* Create new action object */
	raction = p_new(result->pool, struct sieve_result_action, 1);
	raction->action = action;
	raction->context = context;
	
	/* Add */
	if ( result->first_action == NULL ) {
		result->first_action = raction;
		result->last_action = raction;
		raction->prev = NULL;
		raction->next = NULL;
	} else {
		result->last_action->next = raction;
		raction->prev = result->last_action;
		result->last_action = raction;
		raction->next = NULL;
	}	
}	

bool sieve_result_execute(struct sieve_result *result)
{
	struct sieve_result_action *raction = result->first_action;
	
	while ( raction != NULL ) {
		if ( raction->action->perform != NULL ) {
			
		} else {
			i_warning("Action %s performs absolutely nothing.", raction->action->name);	
		}
		raction = raction->next;	
	}
	
	return TRUE;
}
