/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve.h"

#include "managesieve-capabilities.h"

#include <stddef.h>
#include <unistd.h>

/*
 * Capability dumping
 */

void managesieve_capabilities_dump(void)
{
	struct sieve_environment svenv;
	struct sieve_instance *svinst;
	const char *sieve_cap, *notify_cap;

	/* Initialize Sieve engine */

	i_zero(&svenv);
	svenv.home_dir = "/tmp";

	if (sieve_init(&svenv, NULL, NULL, FALSE, &svinst) < 0)
		i_fatal("Failed to initialize Sieve");

	/* Dump capabilities */

	sieve_cap = sieve_get_capabilities(svinst, NULL);
	notify_cap = sieve_get_capabilities(svinst, "notify");

	if (notify_cap == NULL)
		printf("SIEVE: %s\n", sieve_cap);
	else
		printf("SIEVE: %s, NOTIFY: %s\n", sieve_cap, notify_cap);

	sieve_deinit(&svinst);
}
