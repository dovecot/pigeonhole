/* Copyright (c) Pigeonhole authors, see top-level COPYING file */

#include "lib.h"
#include "test-common.h"

#include "sieve-common.h"
#include "sieve-runtime.h"
#include "sieve-runtime-trace.h"
#include "sieve-stringlist.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-match.h"

/* Below the matching trace level, sieve_match() prints nothing, and it must
   leave the trace indent as it found it: an extension may match several
   times within one operation and trace afterwards. */
static void test_sieve_match_trace_indent(void)
{
	static const sieve_trace_level_t levels[] = {
		SIEVE_TRLVL_NONE,
		SIEVE_TRLVL_ACTIONS,
		SIEVE_TRLVL_COMMANDS,
		SIEVE_TRLVL_TESTS,
	};
	const struct sieve_match_type mcht =
		SIEVE_MATCH_TYPE_DEFAULT(is_match_type);
	const struct sieve_comparator cmp =
		SIEVE_COMPARATOR_DEFAULT(i_octet_comparator);
	unsigned int i, j;

	test_begin("sieve_match() trace indent");
	for (i = 0; i < N_ELEMENTS(levels); i++) T_BEGIN {
		struct sieve_runtime_trace trace = {
			.config = { .level = levels[i] },
			.indent = 1,
		};
		struct sieve_runtime_env renv = { .trace = &trace };
		int exec_status;

		for (j = 0; j < 3; j++) {
			(void)sieve_match(
				&renv, &mcht, &cmp,
				sieve_single_stringlist_create_cstr(
					&renv, "value", FALSE),
				sieve_single_stringlist_create_cstr(
					&renv, j % 2 == 0 ? "value" : "key",
					FALSE),
				&exec_status);
			test_assert_idx(exec_status == SIEVE_EXEC_OK, i);
		}
		test_assert_idx(trace.indent == 1, i);
	} T_END;
	test_end();
}

int main(void)
{
	static void (*const test_functions[])(void) = {
		test_sieve_match_trace_indent,
		NULL
	};
	return test_run(test_functions);
}
