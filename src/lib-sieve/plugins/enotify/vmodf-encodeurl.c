/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"

#include "sieve-common.h"
#include "sieve-code.h"

#include "sieve-ext-variables.h"

#include "ext-enotify-common.h"

/*
 * Encodeurl modifier
 */
 
bool mod_encodeurl_modify(string_t *in, string_t **result);
 
const struct sieve_variables_modifier encodeurl_modifier = {
	SIEVE_OBJECT("encodeurl", &encodeurl_operand, 0),
	15,
	mod_encodeurl_modify
};
 
/*
 * Modifier operand
 */

static const struct sieve_extension_obj_registry ext_enotify_modifiers =
	SIEVE_VARIABLES_DEFINE_MODIFIER(encodeurl_modifier);

const struct sieve_operand encodeurl_operand = { 
	"modifier", 
	&enotify_extension,
	0, 
	&sieve_variables_modifier_operand_class,
	&ext_enotify_modifiers
};

/*
 * Modifier implementation
 */

static const char uri_reserved_lookup[256] = {
};

bool mod_encodeurl_modify(string_t *in, string_t **result)
{	
	*result = t_str_new(2*str_len(in));
	
	

	return TRUE;
}
 

