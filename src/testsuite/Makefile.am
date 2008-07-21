noinst_PROGRAMS = testsuite

AM_CPPFLAGS = \
	-I../lib-sieve \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-index \
	-I$(dovecot_incdir)/src/lib-storage \
	-I$(dovecot_incdir)/src/lib-storage/index \
	-I$(dovecot_incdir)/src/lib-storage/index/raw \
	-I$(dovecot_incdir)/src/lib-storage/index/mbox \
	-I$(dovecot_incdir)/src/lib-storage/index/maildir

testsuite_LDFLAGS = -export-dynamic -Wl,--start-group 

libs = \
	$(top_srcdir)/src/lib-sieve/libsieve.la \
	$(dovecot_incdir)/src/lib-storage/list/libstorage_list.a \
	$(dovecot_incdir)/src/lib-storage/index/raw/libstorage_raw.a \
	$(dovecot_incdir)/src/lib-storage/index/maildir/libstorage_maildir.a \
	$(dovecot_incdir)/src/lib-storage/index/mbox/libstorage_mbox.a \
    $(dovecot_incdir)/src/lib-storage/index/libstorage_index.a \
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

cmds = \
	cmd-test.c \
	cmd-test-fail.c \
	cmd-test-set.c

testsuite_SOURCES = \
	namespaces.c \
	mail-raw.c \
	testsuite-common.c \
	testsuite-objects.c \
	$(cmds) \
	ext-testsuite.c \
	testsuite.c

noinst_HEADERS = \
	testsuite-common.h \
	testsuite-objects.h \
	namespaces.h \
	mail-raw.h

# Testsuite tests

EXTRA_DIST = tests

test_cases = \
    tests/testsuite.svtest\
    tests/control-structures.svtest \
	tests/exists.svtest \
	tests/comparators/core.svtest \
	tests/match-types/contains.svtest \
	tests/match-types/matches.svtest \
	tests/address-parts/subaddress.svtest \
	tests/extensions/variables/basic.svtest

$(test_cases): 
	@$(TESTSUITE_BIN) $@
.PHONY: $(test_cases)

test: $(test_cases)
