bin_PROGRAMS = sievec sieve-test sieve-exec

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/lib-sieve \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-index \
	-I$(dovecot_incdir)/src/lib-storage \
	-I$(dovecot_incdir)/src/lib-storage/index \
	-I$(dovecot_incdir)/src/lib-storage/index/raw \
	-I$(dovecot_incdir)/src/lib-storage/index/mbox \
	-I$(dovecot_incdir)/src/lib-storage/index/maildir \
	-I$(dovecot_incdir)/src/deliver

sievec_LDFLAGS = -export-dynamic -Wl,--start-group 
sieve_test_LDFLAGS = -export-dynamic -Wl,--start-group
sieve_exec_LDFLAGS = -export-dynamic -Wl,--start-group

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

sievec_LDADD = $(ldadd)
sieve_test_LDADD = $(ldadd)
sieve_exec_LDADD = $(ldadd)

sievec_DEPENDENCIES = $(libs)
sieve_test_DEPENDENCIES = $(libs)
sieve_exec_DEPENDENCIES = $(libs)

common_sources = \
	bin-common.c \
	namespaces.c \
	mail-raw.c

sievec_SOURCES = \
	sievec.c 

sieve_test_SOURCES = \
	$(common_sources) \
	sieve-test.c 

sieve_exec_SOURCES = \
	$(common_sources) \
	sieve-exec.c 

noinst_HEADERS = \
	bin-common.h \
	namespaces.h \
	mail-raw.h
