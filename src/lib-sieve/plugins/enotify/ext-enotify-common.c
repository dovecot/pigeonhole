#include "lib.h"

#include "sieve-common.h"

#include "ext-enotify-common.h"

/*
 * Notify capability
 */

static const char *ext_notify_get_methods_string(void);

const struct sieve_extension_capabilities notify_capabilities = {
	"notify",
	ext_notify_get_methods_string
};

/*
 * Notify method registry
 */

static const char *ext_notify_get_methods_string(void)
{
	return "mailto";
}

