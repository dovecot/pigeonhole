SUBDIRS = src

EXTRA_DIST = \
	tests \
	examples/* \
	doc/rfc \
	doc/devel \
	COPYING.LGPL \
	ChangeLog

dist_man_MANS = \
	doc/man/sievec.1 \
	doc/man/sieved.1

if MAINTAINER_MODE
ChangeLog: .hg/dirstate
	hg log --style=changelog > ChangeLog
endif

# Testsuite tests

if BUILD_ENOTIFY
ENOTIFY_TESTS = \
	tests/extensions/enotify/basic.svtest
endif

TESTSUITE_BIN = $(top_srcdir)/src/testsuite/testsuite

test_cases = \
	tests/testsuite.svtest\
	tests/control-structures.svtest \
	tests/compile/compile.svtest \
	tests/compile/examples.svtest \
	tests/compile/errors.svtest \
	tests/execute/errors.svtest \
	tests/execute/actions.svtest \
	tests/exists.svtest \
	tests/header.svtest \
	tests/address.svtest \
	tests/lexer.svtest \
	tests/comparators/core.svtest \
	tests/match-types/is.svtest \
	tests/match-types/contains.svtest \
	tests/match-types/matches.svtest \
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
	tests/extensions/imapflags/execute.svtest \
	tests/extensions/body/basic.svtest \
	tests/extensions/body/match-values.svtest \
	tests/extensions/regex/basic.svtest \
	tests/extensions/regex/errors.svtest \
	tests/extensions/reject/execute.svtest \
	tests/extensions/relational/basic.svtest \
	tests/extensions/relational/rfc.svtest \
	tests/extensions/relational/errors.svtest \
	tests/extensions/subaddress/basic.svtest \
	tests/extensions/subaddress/rfc.svtest \
	tests/extensions/vacation/errors.svtest \
	tests/extensions/vacation/execute.svtest \
	$(ENOTIFY_TESTS)

if HAVE_DOVECOT_LIBS

$(test_cases):
	@$(TESTSUITE_BIN) $@

.PHONY: $(test_cases)

test: $(test_cases)

else

test:
	@echo "Cannot compile or execute the testsuite without the Dovecot sources."

endif	
