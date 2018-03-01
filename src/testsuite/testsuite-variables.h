#ifndef __TESTSUITE_VARIABLES_H
#define __TESTSUITE_VARIABLES_H

extern const struct sieve_operand_def testsuite_namespace_operand;

void testsuite_variables_init
	(const struct sieve_extension *this_ext, struct sieve_validator *valdtr);

#endif /* __TESTSUITE_VARIABLES_H */


