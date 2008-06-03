#ifndef __EXT_VACATION_COMMON_H
#define __EXT_VACATION_COMMON_H

#include "sieve-common.h"

/* Commands */

extern const struct sieve_command vacation_command;

/* Operations */

extern const struct sieve_operation vacation_operation;

/* Extension */

extern int ext_vacation_my_id;
extern const struct sieve_extension vacation_extension;

#endif /* __EXT_VACATION_COMMON_H */
