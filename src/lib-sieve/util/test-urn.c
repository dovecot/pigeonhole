/* Copyright (c) 2025 Pigeonhole authors, see the included COPYING file */

#include "lib.h"
#include "net.h"
#include "urn.h"
#include "test-common.h"

struct valid_urn_test {
	const char *urn;
	enum urn_parse_flags flags;

	struct urn urn_parsed;
};

/* Valid HTTP URL tests */
static struct valid_urn_test valid_urn_tests[] = {
	{
		.urn = "urn:frop1234:friep",
		.urn_parsed = {
			.assigned_name = "urn:frop1234:friep",
			.nid = "frop1234",
			.nss = "friep",
		},
	},
	{
		.urn = "urn:example:weather?=op=map&lat=39.56"
			"&lon=-104.85&datetime=1969-07-21T02:56:15Z",
		.urn_parsed = {
			.assigned_name = "urn:example:weather",
			.nid = "example",
			.nss = "weather",
			.enc_q_component = "op=map&lat=39.56"
				"&lon=-104.85&datetime=1969-07-21T02:56:15Z",
		},
	},
};

static unsigned int valid_urn_test_count = N_ELEMENTS(valid_urn_tests);

static void
test_urn_equal(struct urn *urnt, struct urn *urnp)
{
	test_assert(urnp->assigned_name != NULL);
	i_assert(urnt->assigned_name != NULL);
	test_assert(strcmp(urnp->assigned_name, urnt->assigned_name) == 0);

	test_assert(urnp->nid != NULL);
	i_assert(urnt->nid != NULL);
	test_assert(strcmp(urnp->nid, urnt->nid) == 0);

	test_assert(urnp->nss != NULL);
	i_assert(urnt->nss != NULL);
	test_assert(strcmp(urnp->nss, urnt->nss) == 0);

	if (urnp->enc_r_component == NULL || urnt->enc_r_component == NULL) {
		test_assert(urnp->enc_r_component == urnt->enc_r_component);
	} else {
		test_assert(strcmp(urnp->enc_r_component,
				   urnt->enc_r_component) == 0);
	}
	if (urnp->enc_q_component == NULL || urnt->enc_q_component == NULL) {
		test_assert(urnp->enc_q_component == urnt->enc_q_component);
	} else {
		test_assert(strcmp(urnp->enc_q_component,
				   urnt->enc_q_component) == 0);
	}

	if (urnp->enc_f_component == NULL || urnt->enc_f_component == NULL) {
		test_assert(urnp->enc_f_component == urnt->enc_f_component);
	} else {
		test_assert(strcmp(urnp->enc_f_component,
				   urnt->enc_f_component) == 0);
	}
}

static void test_urn_valid(void)
{
	unsigned int i;

	for (i = 0; i < valid_urn_test_count; i++) T_BEGIN {
		const char *urn = valid_urn_tests[i].urn;
		enum urn_parse_flags flags = valid_urn_tests[i].flags;
		struct urn *urnt = &valid_urn_tests[i].urn_parsed;
		struct urn *urnp;
		const char *error = NULL;

		test_begin(t_strdup_printf("urn valid [%d]", i));

		if (urn_parse(urn, flags, pool_datastack_create(),
			      &urnp, &error) < 0)
			urnp = NULL;

		test_out_reason(t_strdup_printf("urn_parse(%s)",
			valid_urn_tests[i].urn), urnp != NULL, error);
		if (urnp != NULL)
			test_urn_equal(urnt, urnp);

		test_end();
	} T_END;
}

struct invalid_urn_test {
	const char *urn;
	enum urn_parse_flags flags;
	struct urn urn_base;
};

static struct invalid_urn_test invalid_urn_tests[] = {
	{
		.urn = "imap://example.com/INBOX",
	},
	{
		.urn = "http:/www.example.com",
	},
	{
		.urn = "urn:-frop:bla",
	},
	{
		.urn = "urn:frop-:bla",
	},
	{
		.urn = "urn:&&&&:bla",
	},
};

static unsigned int invalid_urn_test_count = N_ELEMENTS(invalid_urn_tests);

static void test_urn_invalid(void)
{
	unsigned int i;

	for (i = 0; i < invalid_urn_test_count; i++) T_BEGIN {
		const char *urn = invalid_urn_tests[i].urn;
		enum urn_parse_flags flags = invalid_urn_tests[i].flags;
		struct urn *urnp;
		const char *error = NULL;

		test_begin(t_strdup_printf("urn invalid [%d]", i));

		if (urn_parse(urn, flags, pool_datastack_create(),
			      &urnp, &error) < 0)
			urnp = NULL;
		test_out_reason(t_strdup_printf("parse %s", urn),
				urnp == NULL, error);

		test_end();
	} T_END;

}

static const char *parse_create_urn_tests[] = {
	"urn:example:weather?=op=map&lat=39.56&lon=-104.85&datetime=1969-07-21T02:56:15Z",
};

static unsigned int
parse_create_urn_test_count = N_ELEMENTS(parse_create_urn_tests);

static void test_urn_parse_create(void)
{
	unsigned int i;

	for (i = 0; i < parse_create_urn_test_count; i++) T_BEGIN {
		const char *urn = parse_create_urn_tests[i];
		struct urn *urnp;
		const char *error = NULL;

		test_begin(t_strdup_printf("urn parse/create [%d]", i));

		if (urn_parse(urn, 0, pool_datastack_create(),
			      &urnp, &error) < 0)
			urnp = NULL;
		test_out_reason(t_strdup_printf("parse  %s", urn),
				urnp != NULL, error);
		if (urnp != NULL) {
			const char *urnnew = urn_create(urnp);
			test_out(t_strdup_printf("create %s", urnnew),
				 strcmp(urn, urnnew) == 0);
		}

		test_end();
	} T_END;

}

static void test_urn_equality(void)
{
	const char *error;
	int ret;

	const char *urn_first = "urn:example:a123,z456";
	test_begin("urn all equal [1]");
	ret = urn_equals(urn_first, "URN:example:a123,z456", 0, &error);
	test_out_reason_quiet("equals", ret >= 0, error);
	test_assert(ret > 0);
	test_end();

	test_begin("urn all equal [2]");
	ret = urn_equals(urn_first, "urn:EXAMPLE:a123,z456", 0, &error);
	test_out_reason_quiet("equals", ret >= 0, error);
	test_assert(ret > 0);
	test_end();

	test_begin("urn all equal [3]");
	ret = urn_equals(urn_first, "urn:example:a123,z456?+abc", 0, &error);
	test_out_reason_quiet("equals", ret >= 0, error);
	test_assert(ret > 0);
	test_end();

	test_begin("urn all equal [4]");
	ret = urn_equals(urn_first, "urn:example:a123,z456?=xyz", 0, &error);
	test_out_reason_quiet("equals", ret >= 0, error);
	test_assert(ret > 0);
	test_end();

	test_begin("urn all equal [5]");
	ret = urn_equals(urn_first, "urn:example:a123,z456#789", 0, &error);
	test_out_reason_quiet("equals", ret >= 0, error);
	test_assert(ret > 0);
	test_end();

	test_begin("urn not equal / [1]");
	ret = urn_equals("urn:example:a123,z456/foo",
			 "urn:example:a123,z456/bar", 0, &error);
	test_out_reason_quiet("equals", ret >= 0, error);
	test_assert(ret == 0);
	test_end();

	test_begin("urn not equal / [2]");
	ret = urn_equals("urn:example:a123,z456/foo",
			 "urn:example:a123,z456/baz", 0, &error);
	test_out_reason_quiet("equals", ret >= 0, error);
	test_assert(ret == 0);
	test_end();

	test_begin("urn not equal / [3]");
	ret = urn_equals("urn:example:a123,z456/bar",
			 "urn:example:a123,z456/baz", 0, &error);
	test_out_reason_quiet("equals", ret >= 0, error);
	test_assert(ret == 0);
	test_end();

	test_begin("urn equal pct");
	ret = urn_equals("urn:example:a123%2Cz456", "URN:EXAMPLE:a123%2cz456",
			 0, &error);
	test_out_reason_quiet("equals", ret >= 0, error);
	test_assert(ret > 0);
	test_end();

	test_begin("urn not equal pct [1]");
	ret = urn_equals("urn:example:a123%2Cz456", "urn:example:a123,z456",
			 0, &error);
	test_out_reason_quiet("equals", ret >= 0, error);
	test_assert(ret == 0);
	test_end();

	test_begin("urn not equal pct [2]");
	ret = urn_equals("URN:EXAMPLE:a123%2cz456", "urn:example:a123,z456",
			 0, &error);
	test_out_reason_quiet("equals", ret >= 0, error);
	test_assert(ret == 0);
	test_end();

	test_begin("urn not equal nss case");
	ret = urn_equals("urn:example:A123,z456", "urn:example:a123,Z456",
			 0, &error);
	test_out_reason_quiet("equals", ret >= 0, error);
	test_assert(ret == 0);
	test_end();
}

int main(void)
{
	static void (*const test_functions[])(void) = {
		test_urn_valid,
		test_urn_invalid,
		test_urn_parse_create,
		test_urn_equality,
		NULL
	};
	return test_run(test_functions);
}
