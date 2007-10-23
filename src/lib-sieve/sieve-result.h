#ifndef __SIEVE_RESULT_H
#define __SIEVE_RESULT_H

#include "sieve-interpreter.h"

struct sieve_result;

struct sieve_action {
	const char *name;

	int (*perform)
		(struct sieve_interpreter *interpreter, struct sieve_result *result, void *context);	
	int (*dump)
		(struct sieve_interpreter *interpreter, struct sieve_result *result, void *context);	
};

struct sieve_result *sieve_result_create(void);
void sieve_result_free(struct sieve_result *result);

void sieve_result_add_action
	(struct sieve_result *result, struct sieve_action *action, void *context);		

bool sieve_result_execute(struct sieve_result *result);

#endif
