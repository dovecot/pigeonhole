/* Copyright (c) Pigeonhole authors, see top-level COPYING file */

#include "lib.h"
#include "istream.h"
#include "managesieve-parser.h"
#include "test-common.h"

static struct istream *test_input;

static int
test_parse(const char *line, enum managesieve_parser_flags flags,
	   struct managesieve_parser **parser_r,
	   const struct managesieve_arg **args_r, const char **error_r)
{
	bool fatal;
	int ret;

	test_input = test_istream_create(line);
	*parser_r = managesieve_parser_create(test_input, 1024);

	(void)i_stream_read(test_input);
	ret = managesieve_parser_finish_line(*parser_r, 0, flags, args_r);
	*error_r = managesieve_parser_get_error(*parser_r, &fatal);
	return ret;
}

static void test_parse_deinit(struct managesieve_parser **parser)
{
	managesieve_parser_destroy(parser);
	i_stream_unref(&test_input);
}

static void test_managesieve_parser_finish_line(void)
{
	struct managesieve_parser *parser;
	const struct managesieve_arg *args;
	const char *str, *error;

	test_begin("managesieve parser finish line");

	/* complete line */
	test_assert(test_parse("OK \"Logged in\"\r\n", 0,
			       &parser, &args, &error) == 2);
	test_assert(managesieve_arg_get_atom(&args[0], &str) &&
		    strcmp(str, "OK") == 0);
	test_assert(managesieve_arg_get_string(&args[1], &str) &&
		    strcmp(str, "Logged in") == 0);
	test_assert(MANAGESIEVE_ARG_IS_EOL(&args[2]));
	test_parse_deinit(&parser);

	/* line without CRLF ending with an atom */
	test_assert(test_parse("NO TRYLATER", 0, &parser, &args, &error) == 2);
	test_assert(managesieve_arg_get_atom(&args[1], &str) &&
		    strcmp(str, "TRYLATER") == 0);
	test_assert(MANAGESIEVE_ARG_IS_EOL(&args[2]));
	test_parse_deinit(&parser);

	/* parse error */
	test_assert(test_parse("OK \"\\x\"\r\n", 0,
			       &parser, &args, &error) == -1);
	test_assert_strcmp(error, "Escaped quoted string character is not a QUOTED-SPECIAL.");
	test_parse_deinit(&parser);

	test_end();
}

static void test_managesieve_parser_list(void)
{
	struct managesieve_parser *parser;
	const struct managesieve_arg *args, *list;
	const char *str, *error;
	unsigned int count;

	test_begin("managesieve parser list");

	test_assert(test_parse("OK (SASL \"dGVzdA==\") \"Logged in\"\r\n",
			       MANAGESIEVE_PARSE_FLAG_LIST, &parser,
			       &args, &error) == 3);
	test_assert(error == NULL);
	test_assert(managesieve_arg_get_atom(&args[0], &str) &&
		    strcmp(str, "OK") == 0);
	test_assert(managesieve_arg_get_list_full(&args[1], &list, &count) &&
		    count == 2);
	test_assert(managesieve_arg_get_atom(&list[0], &str) &&
		    strcmp(str, "SASL") == 0);
	test_assert(managesieve_arg_get_string(&list[1], &str) &&
		    strcmp(str, "dGVzdA==") == 0);
	test_assert(MANAGESIEVE_ARG_IS_EOL(&list[2]));
	test_assert(managesieve_arg_get_string(&args[2], &str) &&
		    strcmp(str, "Logged in") == 0);
	test_assert(MANAGESIEVE_ARG_IS_EOL(&args[3]));
	test_parse_deinit(&parser);

	/* list without a following string, and without CRLF */
	test_assert(test_parse("NO (QUOTA/MAXSIZE)",
			       MANAGESIEVE_PARSE_FLAG_LIST, &parser,
			       &args, &error) == 2);
	test_assert(managesieve_arg_get_list_full(&args[1], &list, &count) &&
		    count == 1);
	test_assert(managesieve_arg_get_atom(&list[0], &str) &&
		    strcmp(str, "QUOTA/MAXSIZE") == 0);
	test_assert(MANAGESIEVE_ARG_IS_EOL(&args[2]));
	test_parse_deinit(&parser);

	/* empty list */
	test_assert(test_parse("OK ()\r\n", MANAGESIEVE_PARSE_FLAG_LIST,
			       &parser, &args, &error) == 2);
	test_assert(managesieve_arg_get_list_full(&args[1], &list, &count) &&
		    count == 0);
	test_parse_deinit(&parser);

	test_end();
}

static void test_managesieve_parser_list_errors(void)
{
	static const struct {
		const char *line;
		enum managesieve_parser_flags flags;
		const char *error;
	} tests[] = {
		{ "OK (SASL \"x\")\r\n", 0, "Invalid characters in atom" },
		{ "OK SASL)\r\n", 0, "Invalid characters in atom" },
		{ "OK ((SASL))\r\n", MANAGESIEVE_PARSE_FLAG_LIST,
		  "Nested lists not allowed" },
		{ "OK SASL)\r\n", MANAGESIEVE_PARSE_FLAG_LIST,
		  "Unexpected ')'" },
		{ "OK (SASL \"x\"\r\n", MANAGESIEVE_PARSE_FLAG_LIST,
		  "Missing ')'" },
		{ "OK (SASL", MANAGESIEVE_PARSE_FLAG_LIST, "Missing ')'" },
	};
	struct managesieve_parser *parser;
	const struct managesieve_arg *args;
	const char *error;

	test_begin("managesieve parser list errors");
	for (unsigned int i = 0; i < N_ELEMENTS(tests); i++) {
		test_assert_idx(test_parse(tests[i].line, tests[i].flags,
					   &parser, &args, &error) == -1, i);
		test_assert_strcmp_idx(error, tests[i].error, i);
		test_parse_deinit(&parser);
	}
	test_end();
}

int main(void)
{
	static void (*const test_functions[])(void) = {
		test_managesieve_parser_finish_line,
		test_managesieve_parser_list,
		test_managesieve_parser_list_errors,
		NULL
	};
	return test_run(test_functions);
}
