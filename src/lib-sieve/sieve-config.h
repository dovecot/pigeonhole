#ifndef __SIEVE_CONFIG_H
#define __SIEVE_CONFIG_H

#include "pigeonhole-config.h"
#include "pigeonhole-version.h"

#define SIEVE_IMPLEMENTATION PIGEONHOLE_NAME " Sieve " PIGEONHOLE_VERSION_FULL

#define SIEVE_SCRIPT_FILEEXT "sieve"
#define SIEVE_BINARY_FILEEXT "svbin"

#define DEFAULT_ENVELOPE_SENDER \
	SMTP_ADDRESS_LITERAL("MAILER-DAEMON", NULL)

#define DEFAULT_REDIRECT_DUPLICATE_PERIOD (3600 * 12)

#endif
