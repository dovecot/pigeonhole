#include "lib.h"
#include "mempool.h"

#include "sieve-result.h"

#include <stdio.h>

struct sieve_result_action {
	const struct sieve_action *action;
	void *context;

	struct sieve_result_action *prev, *next; 
};

struct sieve_result {
	pool_t pool;
	
	struct sieve_action_exec_env action_env;
	struct sieve_result_action *first_action;
	struct sieve_result_action *last_action;
	
	unsigned int implicit_keep:1; 
};

struct sieve_result *sieve_result_create(void) 
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

void sieve_result_ref(struct sieve_result *result) 
{
	pool_ref(result->pool);
}

void sieve_result_unref(struct sieve_result **result) 
{
	if ( result != NULL && *result != NULL ) {
		pool_t pool = (*result)->pool;
		pool_unref(&pool);
		if ( pool == NULL )
			*result = NULL;
	}
}

inline pool_t sieve_result_pool(struct sieve_result *result)
{
	return result->pool;
}

bool sieve_result_add_action
(struct sieve_result *result, const struct sieve_action *action, void *context)		
{
	struct sieve_result_action *raction;
	
	/* First, check for duplicates */
	raction = result->first_action;
	while ( raction != NULL ) {
		if ( raction->action == action ) {
			const struct sieve_action *oact = raction->action;
			
			if ( oact->check_duplicate != NULL ) {
				if ( oact->check_duplicate(action, raction->context, context) )
					return FALSE;
			} else 
				return FALSE; 
		}
		raction = raction->next;
	}
	
	/* Check for conflicts */
	
	/* FIXME: Unimplemented */
	
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
	
	return TRUE;
}	

bool sieve_result_print(struct sieve_result *result)
{
	struct sieve_result_action *rac = result->first_action;
	
	printf("\nPerformed actions:\n");
	while ( rac != NULL ) {		
		const struct sieve_action *act = rac->action;
	
		if ( act->print != NULL ) {
			act->print(act, rac->context);	
		} else {
			printf("* %s\n", act->name); 
		}
		rac = rac->next;	
	}
	
	return TRUE;
}

bool sieve_result_execute
	(struct sieve_result *result, const struct sieve_message_data *msgdata,
		const struct sieve_mail_environment *menv)
{ 
	struct sieve_result_action *rac = result->first_action;
	
	result->action_env.msgdata = msgdata;
	result->action_env.mailenv = menv;
	
	printf("\n");
	while ( rac != NULL ) {
		const struct sieve_action *act = rac->action;
	
		if ( act->execute != NULL ) {
			(void) act->execute(act, &result->action_env, rac->context);
		} else {
			i_warning("Action %s performs absolutely nothing.", act->name);	
		}
		rac = rac->next;	
	}
	
	return TRUE;
}
