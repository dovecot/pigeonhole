noinst_PROGRAMS = testsuite

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/lib-sieve \
	-I$(top_srcdir)/src/lib-sieve-tool \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-settings \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-imap \
	-I$(dovecot_incdir)/src/lib-index \
	-I$(dovecot_incdir)/src/lib-master \
	-I$(dovecot_incdir)/src/lib-storage \
	-I$(dovecot_incdir)/src/lib-storage/index \
	-I$(dovecot_incdir)/src/lib-storage/index/raw

testsuite_LDFLAGS = -export-dynamic

libs = \
	$(top_srcdir)/src/lib-sieve/libsieve.la \
	$(top_srcdir)/src/lib-sieve-tool/libsieve-tool.la \
	$(dovecot_incdir)/src/lib-storage/libdovecot-storage.la \
	$(dovecot_incdir)/src/lib-dovecot/libdovecot.la

testsuite_LDADD = $(libs)
testsuite_DEPENDENCIES = $(libs)

commands = \
	cmd-test.c \
	cmd-test-fail.c \
	cmd-test-set.c \
	cmd-test-result-reset.c \
	cmd-test-result-print.c \
	cmd-test-message.c \
	cmd-test-mailbox.c \
	cmd-test-binary.c


tests = \
	tst-test-script-compile.c \
	tst-test-script-run.c \
	tst-test-error.c \
	tst-test-result.c \
	tst-test-result-execute.c

testsuite_SOURCES = \
	testsuite-common.c \
	testsuite-objects.c \
	testsuite-substitutions.c \
	testsuite-arguments.c \
	testsuite-message.c \
	testsuite-log.c \
	testsuite-script.c \
	testsuite-result.c \
	testsuite-smtp.c \
	testsuite-mailstore.c \
	testsuite-binary.c \
	$(commands) \
	$(tests) \
	ext-testsuite.c \
	testsuite.c

noinst_HEADERS = \
	testsuite-common.h \
	testsuite-objects.h \
	testsuite-substitutions.h \
	testsuite-arguments.h \
	testsuite-message.h \
	testsuite-log.h \
	testsuite-script.h \
	testsuite-result.h \
	testsuite-smtp.h \
	testsuite-mailstore.h \
	testsuite-binary.h

