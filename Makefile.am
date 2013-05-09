if BUILD_DOCS
DOCS = doc
endif

SUBDIRS = src $(DOCS)

ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = \
	tests \
	examples \
	COPYING.LGPL \
	ChangeLog

pkginc_libdir=$(dovecot_pkgincludedir)/sieve
nodist_pkginc_lib_HEADERS = pigeonhole-config.h

if MAINTAINER_MODE
ChangeLog: .hg/dirstate
	hg log --style=changelog > ChangeLog
endif

# Testsuite tests (FIXME: ugly)

TESTSUITE_BIN = $(top_builddir)/src/testsuite/testsuite $(TESTSUITE_OPTIONS)

if TESTSUITE_VALGRIND
TEST_BIN = valgrind -q --error-exitcode=1 --show-reachable=yes --leak-check=full $(TESTSUITE_BIN)
else
TEST_BIN = $(TESTSUITE_BIN)
endif

if BUILD_UNFINISHED
test_unfinished =
else
test_unfinished =
endif

test_cases = \
	tests/testsuite.svtest \
	tests/control-if.svtest \
	tests/control-stop.svtest \
	tests/test-allof.svtest \
	tests/test-anyof.svtest \
	tests/test-exists.svtest \
	tests/test-header.svtest \
	tests/test-address.svtest \
	tests/test-size.svtest \
	tests/compile/compile.svtest \
	tests/compile/errors.svtest \
	tests/compile/warnings.svtest \
	tests/compile/recover.svtest \
	tests/execute/errors.svtest \
	tests/execute/actions.svtest \
	tests/execute/smtp.svtest \
	tests/execute/mailstore.svtest \
	tests/execute/examples.svtest \
	tests/lexer.svtest \
	tests/comparators/i-octet.svtest \
	tests/comparators/i-ascii-casemap.svtest \
	tests/match-types/is.svtest \
	tests/match-types/contains.svtest \
	tests/match-types/matches.svtest \
	tests/multiscript/basic.svtest \
	tests/multiscript/conflicts.svtest \
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
	tests/extensions/include/optional.svtest \
	tests/extensions/include/rfc.svtest \
	tests/extensions/include/execute.svtest \
	tests/extensions/imap4flags/basic.svtest \
	tests/extensions/imap4flags/hasflag.svtest \
	tests/extensions/imap4flags/execute.svtest \
	tests/extensions/imap4flags/multiscript.svtest \
	tests/extensions/imap4flags/flagstring.svtest \
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
	tests/extensions/relational/comparators.svtest \
	tests/extensions/subaddress/basic.svtest \
	tests/extensions/subaddress/rfc.svtest \
	tests/extensions/subaddress/config.svtest \
	tests/extensions/vacation/errors.svtest \
	tests/extensions/vacation/execute.svtest \
	tests/extensions/vacation/message.svtest \
	tests/extensions/vacation/smtp.svtest \
	tests/extensions/vacation/utf-8.svtest \
	tests/extensions/vacation/reply.svtest \
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
	tests/extensions/spamvirustest/spamtest.svtest \
	tests/extensions/spamvirustest/virustest.svtest \
	tests/extensions/spamvirustest/spamtestplus.svtest \
	tests/extensions/spamvirustest/errors.svtest \
	tests/extensions/ihave/execute.svtest \
	tests/extensions/ihave/errors.svtest \
	tests/extensions/ihave/restrictions.svtest \
	tests/extensions/editheader/addheader.svtest \
	tests/extensions/editheader/deleteheader.svtest \
	tests/extensions/editheader/alternating.svtest \
	tests/extensions/editheader/utf8.svtest \
	tests/extensions/editheader/protected.svtest \
	tests/extensions/editheader/errors.svtest \
	tests/extensions/vnd.dovecot/debug/execute.svtest \
	tests/extensions/vnd.dovecot/duplicate/errors.svtest \
	tests/extensions/vnd.dovecot/duplicate/execute.svtest \
	tests/deprecated/notify/basic.svtest \
	tests/deprecated/notify/mailto.svtest \
	tests/deprecated/notify/errors.svtest \
	tests/deprecated/notify/execute.svtest \
	tests/deprecated/notify/denotify.svtest \
	tests/deprecated/imapflags/execute.svtest \
	tests/deprecated/imapflags/errors.svtest \
	$(test_unfinished)

$(test_cases):
	@$(TEST_BIN) $(top_srcdir)/$@

TEST_EXTPROGRAMS_BIN = $(TEST_BIN) \
	-P src/plugins/sieve-extprograms/.libs/sieve_extprograms

extprograms_test_cases = \
	tests/plugins/extprograms/errors.svtest \
	tests/plugins/extprograms/pipe/command.svtest \
	tests/plugins/extprograms/pipe/errors.svtest \
	tests/plugins/extprograms/pipe/execute.svtest \
	tests/plugins/extprograms/filter/command.svtest \
	tests/plugins/extprograms/filter/errors.svtest \
	tests/plugins/extprograms/filter/execute.svtest \
	tests/plugins/extprograms/execute/command.svtest \
	tests/plugins/extprograms/execute/errors.svtest \
	tests/plugins/extprograms/execute/execute.svtest

$(extprograms_test_cases):
	@$(TEST_EXTPROGRAMS_BIN) 	$(top_srcdir)/$@

.PHONY: $(test_cases) $(extprograms_test_cases)
test: $(test_cases)
test-plugins: $(extprograms_test_cases)

check: check-am test all-am
