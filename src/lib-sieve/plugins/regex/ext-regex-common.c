/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-match-types.h"

#include "ext-regex-common.h"

/*
 * Regex match type operand
 */

static const struct sieve_extension_objects ext_match_types =
	SIEVE_EXT_DEFINE_MATCH_TYPE(regex_match_type);

const struct sieve_operand_def regex_match_type_operand = {
	.name = "regex match",
	.ext_def = &regex_extension,
	.class = &sieve_match_type_operand_class,
	.interface = &ext_match_types
};

