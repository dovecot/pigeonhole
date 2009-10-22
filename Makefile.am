SUBDIRS = src doc

EXTRA_DIST = \
	tests \
	examples \
	COPYING.LGPL \
	ChangeLog

if MAINTAINER_MODE
ChangeLog: .hg/dirstate
	hg log --style=changelog > ChangeLog
endif

# Testsuite tests

TESTSUITE_BIN = $(top_srcdir)/src/testsuite/testsuite

if TESTSUITE_VALGRIND
TEST_BIN = valgrind -q --error-exitcode=1 --show-reachable=yes --leak-check=full $(TESTSUITE_BIN) 
else
TEST_BIN = $(TESTSUITE_BIN)
endif

test_cases = \
	tests/testsuite.svtest\
	tests/control-structures.svtest \
	tests/compile/compile.svtest \
	tests/compile/errors.svtest \
	tests/compile/warnings.svtest \
	tests/execute/errors.svtest \
	tests/execute/actions.svtest \
	tests/execute/smtp.svtest \
	tests/execute/mailstore.svtest \
	tests/execute/examples.svtest \
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
	tests/extensions/include/errors.svtest \
	tests/extensions/include/variables.svtest \
	tests/extensions/include/once.svtest \
	tests/extensions/include/twice.svtest \
	tests/extensions/include/rfc.svtest \
	tests/extensions/include/execute.svtest \
	tests/extensions/imap4flags/basic.svtest \
	tests/extensions/imap4flags/hasflag.svtest \
	tests/extensions/imap4flags/errors.svtest \
	tests/extensions/imap4flags/execute.svtest \
	tests/extensions/imap4flags/multiscript.svtest \
	tests/extensions/imap4flags/flagstore.svtest \
	tests/extensions/body/basic.svtest \
	tests/extensions/body/raw.svtest \
	tests/extensions/body/content.svtest \
	tests/extensions/body/match-values.svtest \
	tests/extensions/regex/basic.svtest \
	tests/extensions/regex/match-values.svtest \
	tests/extensions/regex/errors.svtest \
	tests/extensions/reject/execute.svtest \
	tests/extensions/reject/smtp.svtest \
	tests/extensions/relational/basic.svtest \
	tests/extensions/relational/rfc.svtest \
	tests/extensions/relational/errors.svtest \
	tests/extensions/subaddress/basic.svtest \
	tests/extensions/subaddress/rfc.svtest \
	tests/extensions/vacation/errors.svtest \
	tests/extensions/vacation/execute.svtest \
	tests/extensions/vacation/message.svtest \
	tests/extensions/vacation/smtp.svtest \
	tests/extensions/enotify/basic.svtest \
	tests/extensions/enotify/encodeurl.svtest \
	tests/extensions/enotify/valid_notify_method.svtest \
	tests/extensions/enotify/notify_method_capability.svtest \
	tests/extensions/enotify/errors.svtest \
	tests/extensions/enotify/execute.svtest \
	tests/extensions/enotify/mailto.svtest \
	tests/extensions/environment/basic.svtest \
	tests/extensions/environment/rfc.svtest \
	tests/extensions/mailbox/execute.svtest \
	tests/extensions/date/basic.svtest \
	tests/extensions/date/date-parts.svtest \
	tests/extensions/date/zones.svtest \
	tests/multiscript/basic.svtest \
	tests/deprecated/notify/basic.svtest \
	tests/deprecated/notify/mailto.svtest \
	tests/deprecated/notify/errors.svtest \
	tests/deprecated/notify/execute.svtest

if HAVE_DOVECOT_LIBS

$(test_cases):
	@$(TEST_BIN) $@

.PHONY: $(test_cases)

test: $(test_cases)

else

test:
	@echo "Cannot compile or execute the testsuite without the Dovecot sources."

endif	
