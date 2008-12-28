noinst_PROGRAMS = testsuite

AM_CPPFLAGS = \
	-I../lib-sieve \
	-I../lib-sieve-tool \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-index \
	-I$(dovecot_incdir)/src/lib-storage

testsuite_LDFLAGS = -export-dynamic -Wl,--start-group 

libs = \
	$(top_srcdir)/src/lib-sieve/libsieve.la \
	$(top_srcdir)/src/lib-sieve-tool/libsieve-tool.la \
	$(dovecot_incdir)/src/lib-storage/list/libstorage_list.a \
	$(dovecot_incdir)/src/lib-storage/register/libstorage-register.a \
	$(STORAGE_LIBS) \
	$(dovecot_incdir)/src/lib-storage/libstorage.a \
	$(dovecot_incdir)/src/lib-index/libindex.a \
	$(dovecot_incdir)/src/lib-imap/libimap.a \
	$(dovecot_incdir)/src/lib-mail/libmail.a \
	$(dovecot_incdir)/src/lib-charset/libcharset.a \
	$(dovecot_incdir)/src/lib/liblib.a

ldadd = \
	$(libs) \
 	$(LIBICONV) \
	$(RAND_LIBS) \
	$(MODULE_LIBS)

testsuite_LDADD = $(ldadd)
testsuite_DEPENDENCIES = $(libs)

commands = \
	cmd-test.c \
	cmd-test-fail.c \
	cmd-test-set.c

tests = \
	tst-test-script-compile.c \
	tst-test-script-run.c \
	tst-test-error.c \
	tst-test-result.c

testsuite_SOURCES = \
	testsuite-common.c \
	testsuite-objects.c \
	testsuite-substitutions.c \
	testsuite-arguments.c \
	testsuite-result.c \
	$(commands) \
	$(tests) \
	ext-testsuite.c \
	testsuite.c

noinst_HEADERS = \
	testsuite-common.h \
	testsuite-objects.h \
	testsuite-substitutions.h \
	testsuite-arguments.h \
	testsuite-result.h

