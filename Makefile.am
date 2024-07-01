aclocaldir = $(datadir)/aclocal

if BUILD_DOCS
DOCS = doc
endif

SUBDIRS = \
	. \
	src \
	$(DOCS)

ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = \
	version \
	tests \
	examples \
	COPYING.LGPL \
	ChangeLog \
	update-version.sh \
	build-aux/git-abi-version-gen \
	build-aux/git-version-gen \
	tests/test-address.svtest.in

dist-hook:
	rm -rf `find $(distdir)/tests -type f -name '*.svbin'`

pkginc_libdir=$(dovecot_pkgincludedir)/sieve
dist_pkginc_lib_HEADERS = \
    pigeonhole-version.h
nodist_pkginc_lib_HEADERS = \
	pigeonhole-config.h

ChangeLog:
	git log --name-status \
		--pretty="format:%ai %aN <%aE> (%h)%n%n%w(80,4,4)%s%n%n%b" > ChangeLog \
			|| rm -f ChangeLog

dist_aclocal_DATA = dovecot-pigeonhole.m4

pigeonhole-version.h: noop
	$(SHELL) $(top_srcdir)/update-version.sh $(top_srcdir) $(top_builddir)

noop:

DISTCLEANFILES = \
    $(top_builddir)/pigeonhole-version.h \
	$(top_builddir)/run-test.sh

# Testsuite tests (FIXME: ugly)

TESTSUITE_BIN = $(top_builddir)/src/testsuite/testsuite $(TESTSUITE_OPTIONS)

TEST_BIN = $(RUN_TEST) $(TESTSUITE_BIN)

if BUILD_UNFINISHED
test_unfinished =
else
test_unfinished =
endif

test_utf8_cases = \
		  tests/test-address.svtest

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
	tests/execute/errors-cpu-limit.svtest \
	tests/execute/actions.svtest \
	tests/execute/smtp.svtest \
	tests/execute/mailstore.svtest \
	tests/execute/address-normalize.svtest \
	tests/execute/examples.svtest \
	tests/lexer.svtest \
	tests/comparators/i-octet.svtest \
	tests/comparators/i-ascii-casemap.svtest \
	tests/comparators/i-unicode-casemap.svtest \
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
	tests/extensions/imap4flags/errors.svtest \
	tests/extensions/imap4flags/hasflag.svtest \
	tests/extensions/imap4flags/execute.svtest \
	tests/extensions/imap4flags/multiscript.svtest \
	tests/extensions/imap4flags/flagstring.svtest \
	tests/extensions/imap4flags/flagstore.svtest \
	tests/extensions/body/basic.svtest \
	tests/extensions/body/errors.svtest \
	tests/extensions/body/raw.svtest \
	tests/extensions/body/content.svtest \
	tests/extensions/body/text.svtest \
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
	tests/extensions/mailbox/errors.svtest \
	tests/extensions/mailbox/execute.svtest \
	tests/extensions/date/basic.svtest \
	tests/extensions/date/date-parts.svtest \
	tests/extensions/date/zones.svtest \
	tests/extensions/index/basic.svtest \
	tests/extensions/index/errors.svtest \
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
	tests/extensions/editheader/execute.svtest \
	tests/extensions/extlists/errors.svtest \
	tests/extensions/extlists/basic.svtest \
	tests/extensions/extlists/multiple.svtest \
	tests/extensions/extlists/valid_ext_list.svtest \
	tests/extensions/duplicate/errors.svtest \
	tests/extensions/duplicate/execute.svtest \
	tests/extensions/metadata/execute.svtest \
	tests/extensions/metadata/errors.svtest \
	tests/extensions/mime/errors.svtest \
	tests/extensions/mime/header.svtest \
	tests/extensions/mime/exists.svtest \
	tests/extensions/mime/address.svtest \
	tests/extensions/mime/execute.svtest \
	tests/extensions/mime/content-header.svtest \
	tests/extensions/mime/foreverypart.svtest \
	tests/extensions/mime/extracttext.svtest \
	tests/extensions/mime/calendar-example.svtest \
	tests/extensions/special-use/errors.svtest \
	tests/extensions/special-use/execute.svtest \
	tests/extensions/vnd.dovecot/debug/execute.svtest \
	tests/extensions/vnd.dovecot/environment/basic.svtest \
	tests/extensions/vnd.dovecot/environment/variables.svtest \
	tests/extensions/vnd.dovecot/report/errors.svtest \
	tests/extensions/vnd.dovecot/report/execute.svtest \
	$(test_unfinished)


prepare_test_cases:
	$(AM_V_at)for case in $(test_utf8_cases); do \
		if test "$(DOVECOT_HAVE_MAIL_UTF8)" = yes; then \
		  $(SED) -s -e 's/#UTF8#//' $${case}.in > $$case ; \
		else \
		  $(SED) -s -e 's/#UTF8#.*//' $${case}.in > $$case ; \
		fi; \
	done

$(test_cases): prepare_test_cases
	@$(TEST_BIN) $(top_srcdir)/$@

failure_test_cases = \
	tests/failures/fuzz1.svtest \
	tests/failures/fuzz2.svtest \
	tests/failures/fuzz3.svtest \
	tests/failures/mailbox-bad-utf8.svtest

$(failure_test_cases):
	@$(TEST_BIN) -F $(top_srcdir)/$@

TEST_EXTPROGRAMS_BIN = NOCHILDREN=yes $(TEST_BIN) \
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

.PHONY: test test-plugins $(test_cases) $(failure_test_cases) $(extprograms_test_cases) prepare_test_case
test: all-am $(test_cases) $(failure_test_cases)
test-plugins: all-am $(extprograms_test_cases)

check: check-am test

version:
	$(AM_V_GEN)env VERSION=$(PACKAGE_VERSION) $(top_srcdir)/build-aux/git-version-gen > $@
