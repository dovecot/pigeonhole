/* Copyright (c) 2002-2016 Pigeonhole authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-interpreter.h"

#include "ext-mime-common.h"

struct ext_foreverypart_runtime_loop *
ext_foreverypart_runtime_loop_get_current
(const struct sieve_runtime_env *renv)
{
	struct sieve_interpreter_loop *loop;
	struct ext_foreverypart_runtime_loop *fploop;

	loop = sieve_interpreter_loop_get_global
		(renv->interp, NULL, &foreverypart_extension);
	if ( loop == NULL ) {
		fploop = NULL;
	} else {
		fploop = (struct ext_foreverypart_runtime_loop *)
			sieve_interpreter_loop_get_context(loop);
		i_assert(fploop->part != NULL);
	}

	return fploop;
}
