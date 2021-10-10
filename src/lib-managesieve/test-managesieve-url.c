/* Copyright (c) 2021 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "net.h"
#include "managesieve-url.h"
#include "test-common.h"

struct valid_managesieve_url_test {
	const char *url;
	enum managesieve_url_parse_flags flags;

	struct managesieve_url url_parsed;
};

/* Valid MANAGESIEVE URL tests */
static struct valid_managesieve_url_test valid_url_tests[] = {
	/* Generic tests */
	{
		.url = "sieve://localhost",
		.url_parsed = {
			.host = { .name = "localhost" },
		},
	},
	{
		.url = "sieve://www.%65%78%61%6d%70%6c%65.com",
		.url_parsed = {
			.host = { .name = "www.example.com" },
		},
	},
	{
		.url = "sieve://www.dovecot.org:8080",
		.url_parsed = {
			.host = { .name = "www.dovecot.org" },
			.port = 8080,
		},
	},
	{
		.url = "sieve://127.0.0.1",
		.url_parsed = {
			.host = {
				.name = "127.0.0.1",
				.ip = { .family = AF_INET },
			},
		},
	},
	{
		.url = "sieve://[::1]",
		.url_parsed = {
			.host = {
				.name = "[::1]",
				.ip = { .family = AF_INET6 },
			},
		},
	},
	{
		.url = "sieve://[::1]:8080",
		.url_parsed = {
			.host = {
				.name = "[::1]",
				.ip = { .family = AF_INET6 },
			},
			.port = 8080,
		},
	},
	{
		.url = "sieve://user@api.dovecot.org",
		.flags = MANAGESIEVE_URL_ALLOW_USERINFO_PART,
		.url_parsed = {
			.host = { .name = "api.dovecot.org" },
			.user = "user",
		},
	},
	{
		.url = "sieve://userid:secret@api.dovecot.org",
		.flags = MANAGESIEVE_URL_ALLOW_USERINFO_PART,
		.url_parsed = {
			.host = { .name = "api.dovecot.org" },
			.user = "userid",
			.password = "secret",
		},
	},
	{
		.url = "sieve://su%3auserid:secret@api.dovecot.org",
		.flags = MANAGESIEVE_URL_ALLOW_USERINFO_PART,
		.url_parsed = {
			.host = { .name = "api.dovecot.org" },
			.user = "su:userid",
			.password = "secret",
		},
	},
	{
		.url = "sieve://www.example.com/",
		.url_parsed = {
			.host = { .name = "www.example.com" },
			.scriptname = "",
		},
	},
	{
		.url = "sieve://www.example.com/frop",
		.url_parsed = {
			.host = { .name = "www.example.com" },
			.scriptname = "frop",
		},
	},
	{
		.url = "sieve://www.example.com/user/frop",
		.url_parsed = {
			.host = { .name = "www.example.com" },
			.owner = "user",
			.scriptname = "frop",
		},
	},
};

static unsigned int valid_url_test_count = N_ELEMENTS(valid_url_tests);

static void
test_managesieve_url_equal(struct managesieve_url *urlt, struct managesieve_url *urlp)
{
	if (urlp->host.name == NULL || urlt->host.name == NULL) {
		test_assert(urlp->host.name == urlt->host.name);
	} else {
		test_assert(strcmp(urlp->host.name, urlt->host.name) == 0);
	}
	test_assert(urlp->port == urlt->port);
	test_assert(urlp->host.ip.family == urlt->host.ip.family);
	if (urlp->user == NULL || urlt->user == NULL) {
		test_assert(urlp->user == urlt->user);
	} else {
		test_assert(strcmp(urlp->user, urlt->user) == 0);
	}
	if (urlp->password == NULL || urlt->password == NULL) {
		test_assert(urlp->password == urlt->password);
	} else {
		test_assert(strcmp(urlp->password, urlt->password) == 0);
	}
	if (urlp->owner == NULL || urlt->owner == NULL) {
		test_assert(urlp->owner == urlt->owner);
	} else {
		test_assert(strcmp(urlp->owner, urlt->owner) == 0);
	}
	if (urlp->scriptname == NULL || urlt->scriptname == NULL) {
		test_assert(urlp->scriptname == urlt->scriptname);
	} else {
		test_assert(strcmp(urlp->scriptname, urlt->scriptname) == 0);
	}
}

static void test_managesieve_url_valid(void)
{
	unsigned int i;

	for (i = 0; i < valid_url_test_count; i++) T_BEGIN {
		const char *url = valid_url_tests[i].url;
		enum managesieve_url_parse_flags flags =
			valid_url_tests[i].flags;
		struct managesieve_url *urlt = &valid_url_tests[i].url_parsed;
		struct managesieve_url *urlp;
		const char *error = NULL;

		test_begin(t_strdup_printf("managesieve url valid [%d]", i));

		if (managesieve_url_parse(url, flags,
					  pool_datastack_create(),
					  &urlp, &error) < 0)
			urlp = NULL;

		test_out_reason(t_strdup_printf("managesieve_url_parse(%s)",
				valid_url_tests[i].url), urlp != NULL, error);
		if (urlp != NULL)
			test_managesieve_url_equal(urlt, urlp);

		test_end();
	} T_END;
}

struct invalid_managesieve_url_test {
	const char *url;
	enum managesieve_url_parse_flags flags;
	struct managesieve_url url_base;
};

static struct invalid_managesieve_url_test invalid_url_tests[] = {
	{
		.url = "imap://example.com/INBOX"
	},
	{
		.url = "managesieve:/www.example.com"
	},
	{
		.url = ""
	},
	{
		.url = "/frop"
	},
	{
		.url = "sieve//www.example.com/frop\""
	},
	{
		.url = "sieve:///dovecot.org"
	},
	{
		.url = "sieve://[]/frop"
	},
	{
		.url = "sieve://[v08.234:232:234:234:2221]/user/frop"
	},
	{
		.url = "sieve://[1::34a:34:234::6]/frop"
	},
	{
		.url = "sieve://example%a.com/frop"
	},
	{
		.url = "sieve://example.com%/user/frop"
	},
	{
		.url = "sieve://example%00.com/frop"
	},
	{
		.url = "sieve://example.com:65536/frop"
	},
	{
		.url = "sieve://example.com:72817/frop"
	},
	{
		.url = "sieve://example.com/user/%00"
	},
	{
		.url = "sieve://example.com/user/%0r"
	},
	{
		.url = "sieve://example.com/user/%"
	},
	{
		.url = "sieve://example.com/?%00"
	},
	{
		.url = "sieve://example.com/user/"
	},
	{
		.url = "sieve://example.com/user/frop/frml"
	},
	{
		.url = "sieve://www.example.com/user/frop#IMAP_Server"
	},
	{
		.url = "sieve://example.com/#%00",
	},
	{
		.url = "sieve://example.com/?query"
	},
	{
		.url = "sieve://example.com/user?query"
	},
	{
		.url = "sieve://example.com/?query/user"
	},
	{
		.url = "sieve://example.com/#fragment"
	},
	{
		.url = "sieve://example.com/user#fragment"
	},
	{
		.url = "sieve://example.com/#fragment/user"
	},
};

static unsigned int invalid_url_test_count = N_ELEMENTS(invalid_url_tests);

static void test_managesieve_url_invalid(void)
{
	unsigned int i;

	for (i = 0; i < invalid_url_test_count; i++) T_BEGIN {
		const char *url = invalid_url_tests[i].url;
		enum managesieve_url_parse_flags flags = invalid_url_tests[i].flags;
		struct managesieve_url *urlp;
		const char *error = NULL;

		test_begin(t_strdup_printf("managesieve url invalid [%d]", i));

		if (managesieve_url_parse(url, flags, pool_datastack_create(),
					  &urlp, &error) < 0)
			urlp = NULL;
		test_out_reason(t_strdup_printf("parse %s", url),
				urlp == NULL, error);

		test_end();
	} T_END;

}

static const char *parse_create_url_tests[] = {
	"sieve://www.example.com/",
	"sieve://10.0.0.1/",
	"sieve://[::1]/",
	"sieve://www.example.com:993/",
	"sieve://www.example.com/frop",
	"sieve://www.example.com/user/frop",
	"sieve://www.example.com/user/%23shared",
};

static unsigned int
parse_create_url_test_count = N_ELEMENTS(parse_create_url_tests);

static void test_managesieve_url_parse_create(void)
{
	unsigned int i;

	for (i = 0; i < parse_create_url_test_count; i++) T_BEGIN {
		const char *url = parse_create_url_tests[i];
		struct managesieve_url *urlp;
		const char *error = NULL;

		test_begin(t_strdup_printf("managesieve url parse/create [%d]", i));

		if (managesieve_url_parse(url, 0, pool_datastack_create(),
					  &urlp, &error) < 0)
			urlp = NULL;
		test_out_reason(t_strdup_printf("parse  %s", url),
				urlp != NULL, error);
		if (urlp != NULL) {
			const char *urlnew = managesieve_url_create(urlp);
			test_out(t_strdup_printf("create %s", urlnew),
				 strcmp(url, urlnew) == 0);
		}

		test_end();
	} T_END;

}

int main(void)
{
	static void (*const test_functions[])(void) = {
		test_managesieve_url_valid,
		test_managesieve_url_invalid,
		test_managesieve_url_parse_create,
		NULL
	};
	return test_run(test_functions);
}
