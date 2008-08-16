SUBDIRS = src

EXTRA_DIST = \
	tests \
	sieve/* \
	doc/* \
    COPYING.LGPL \
	DESIGN \
    ChangeLog

if MAINTAINER_MODE
ChangeLog: .hg/dirstate
	hg log --style=changelog > ChangeLog
endif

# Testsuite tests

TESTSUITE_BIN = $(top_srcdir)/src/testsuite/testsuite

test_cases = \
	tests/testsuite.svtest\
	tests/control-structures.svtest \
	tests/exists.svtest \
	tests/lexer.svtest \
	tests/comparators/core.svtest \
	tests/match-types/is.svtest \
	tests/match-types/contains.svtest \
	tests/match-types/matches.svtest \
	tests/match-types/relational.svtest \
	tests/address-parts/subaddress.svtest \
	tests/extensions/encoded-character.svtest \
	tests/extensions/envelope.svtest \
	tests/extensions/variables/basic.svtest \
	tests/extensions/variables/match.svtest \
	tests/extensions/variables/modifiers.svtest \
	tests/extensions/variables/quoting.svtest \
	tests/extensions/variables/string.svtest \
	tests/extensions/variables/errors.svtest \
	tests/extensions/variables/regex.svtest \
	tests/extensions/include/variables.svtest \
	tests/extensions/include/errors.svtest \
	tests/extensions/imapflags/basic.svtest \
	tests/extensions/imapflags/hasflag.svtest \
	tests/extensions/body/basic.svtest \
	tests/extensions/regex/basic.svtest \
	tests/extensions/regex/errors.svtest \
	tests/compile/compile.svtest \
	tests/compile/compile-examples.svtest \
	tests/compile/errors.svtest

$(test_cases):
	@$(TESTSUITE_BIN) $@

.PHONY: $(test_cases)

test: $(test_cases)

