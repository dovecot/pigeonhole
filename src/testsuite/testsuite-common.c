/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "string.h"
#include "ostream.h"
#include "hash.h"
#include "mail-storage.h"
#include "env-util.h"
#include "unlink-directory.h"

#include "mail-raw.h"

#include "sieve-common.h"
#include "sieve-code.h"
#include "sieve-message.h"
#include "sieve-commands.h"
#include "sieve-extensions.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-result.h"
#include "sieve-dump.h"

#include "testsuite-common.h"
#include "testsuite-objects.h"
#include "testsuite-log.h"
#include "testsuite-script.h"
#include "testsuite-result.h"
#include "testsuite-smtp.h"

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

/*
 * Global data
 */

/* Test context */

static string_t *test_name;
unsigned int test_index;
unsigned int test_failures;

/* 
 * Validator context 
 */

bool testsuite_validator_context_initialize(struct sieve_validator *valdtr)
{
	pool_t pool = sieve_validator_pool(valdtr);
	struct testsuite_validator_context *ctx = 
		p_new(pool, struct testsuite_validator_context, 1);
	
	/* Setup object registry */
	ctx->object_registrations = sieve_validator_object_registry_create(valdtr);
	testsuite_register_core_objects(ctx);
	
	sieve_validator_extension_set_context(valdtr, &testsuite_extension, ctx);

	return TRUE;
}

struct testsuite_validator_context *testsuite_validator_context_get
(struct sieve_validator *valdtr)
{
	return (struct testsuite_validator_context *)
		sieve_validator_extension_get_context(valdtr, &testsuite_extension);
}

/* 
 * Generator context 
 */

bool testsuite_generator_context_initialize(struct sieve_generator *gentr)
{
	pool_t pool = sieve_generator_pool(gentr);
	struct sieve_binary *sbin = sieve_generator_get_binary(gentr);
	struct testsuite_generator_context *ctx = 
		p_new(pool, struct testsuite_generator_context, 1);
	
	/* Setup exit jumplist */
	ctx->exit_jumps = sieve_jumplist_create(pool, sbin);
	
	sieve_generator_extension_set_context(gentr, &testsuite_extension, ctx);

	return TRUE;
}

/*
 * Test context
 */
 
static void testsuite_test_context_init(void)
{
	test_name = str_new(default_pool, 128);
	test_index = 0;	
	test_failures = 0;
}

void testsuite_test_start(string_t *name)
{
	str_truncate(test_name, 0);
	str_append_str(test_name, name);

	test_index++;
}

void testsuite_test_fail(string_t *reason)
{
	testsuite_test_fail_cstr(str_c(reason));
}

void testsuite_test_failf(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	testsuite_test_fail_cstr(t_strdup_vprintf(fmt, args));

	va_end(args);
}

void testsuite_test_fail_cstr(const char *reason)
{	
	if ( str_len(test_name) == 0 ) {
		if ( reason == NULL || *reason == '\0' )
			printf("%2d: Test FAILED\n", test_index);
		else
			printf("%2d: Test FAILED: %s\n", test_index, reason);
	} else {
		if ( reason == NULL || *reason == '\0' )
			printf("%2d: Test '%s' FAILED\n", test_index, str_c(test_name));
		else
			printf("%2d: Test '%s' FAILED: %s\n", test_index, 
				str_c(test_name), reason);
	}

	str_truncate(test_name, 0);

	test_failures++;
}

void testsuite_testcase_fail(const char *reason)
{	
	if ( reason == NULL || *reason == '\0' )
		printf("XX: Test CASE FAILED\n");
	else
		printf("XX: Test CASE FAILED: %s\n", reason);

	test_failures++;
}

void testsuite_test_succeed(string_t *reason)
{
	if ( str_len(test_name) == 0 ) {
		if ( reason == NULL || str_len(reason) == 0 )
			printf("%2d: Test SUCCEEDED\n", test_index);
		else
			printf("%2d: Test SUCCEEDED: %s\n", test_index, str_c(reason));
	} else {
		if ( reason == NULL || str_len(reason) == 0 )
			printf("%2d: Test '%s' SUCCEEDED\n", test_index, str_c(test_name));
		else
			printf("%2d: Test '%s' SUCCEEDED: %s\n", test_index, 
				str_c(test_name), str_c(reason));
	}
	str_truncate(test_name, 0);
}

static void testsuite_test_context_deinit(void)
{
	//str_free(test_name);
}

int testsuite_testcase_result(void)
{
	if ( test_failures > 0 ) {
		printf("\nFAIL: %d of %d tests failed.\n\n", test_failures, test_index);
		return 1;
	}

	printf("\nPASS: %d tests succeeded.\n\n", test_index);
	return 0;
}

/*
 * Testsuite temporary directory
 */

static char *testsuite_tmp_dir;

static void testsuite_tmp_dir_init(void)
{
	testsuite_tmp_dir = i_strdup_printf
		("/tmp/dsieve-testsuite.%s.%s", dec2str(time(NULL)), dec2str(getpid()));

	if ( mkdir(testsuite_tmp_dir, 0700) < 0 ) {
		i_fatal("failed to create temporary directory '%s': %m.", 
			testsuite_tmp_dir);		
	}
}

static void testsuite_tmp_dir_deinit(void)
{
	if ( unlink_directory(testsuite_tmp_dir, TRUE) < 0 )
		i_warning("failed to remove temporary directory '%s': %m.",
			testsuite_tmp_dir);

	i_free(testsuite_tmp_dir);
}

const char *testsuite_tmp_dir_get(void)
{
	return testsuite_tmp_dir;
}

/*
 * Main testsuite init/deinit
 */

void testsuite_init(void)
{
	testsuite_test_context_init();
	testsuite_log_init();
	testsuite_tmp_dir_init();
	
	testsuite_script_init();
	testsuite_smtp_init();
}

void testsuite_deinit(void)
{
	testsuite_smtp_deinit();
	testsuite_script_deinit();
	
	testsuite_tmp_dir_deinit();
	testsuite_log_deinit();
	testsuite_test_context_deinit();
}

