#include "sieve-common.h"
#include "sieve-match-types.h"

#include "ext-regex-common.h"

/* 
 * Regex match type operand
 */

static const struct sieve_match_type_operand_interface match_type_operand_intf =
{
    SIEVE_EXT_DEFINE_MATCH_TYPE(regex_match_type)
};

const struct sieve_operand regex_match_type_operand = {
    "regex match",
    &regex_extension,
    0,
    &sieve_match_type_operand_class,
    &match_type_operand_intf
};

