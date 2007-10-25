bin_PROGRAMS = sievec sieve_test

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/lib-sieve \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-index \
	-I$(dovecot_incdir)/src/lib-storage \
	-I$(dovecot_incdir)/src/lib-storage/index \
	-I$(dovecot_incdir)/src/lib-storage/index/mbox 

plugin_dir = \
	$(top_srcdir)/src/lib-sieve/plugins

plugins = \
	$(plugin_dir)/vacation/lib_ext_vacation.a

sievec_LDFLAGS = -export-dynamic
sieve_test_LDFLAGS = -export-dynamic

libs = \
	$(top_srcdir)/src/lib-sieve/libsieve.a \
	$(dovecot_incdir)/src/lib-storage/list/libstorage_list.a \
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
	$(plugins) \
 	$(LIBICONV) \
    $(RAND_LIBS) \
    $(MODULE_LIBS)

sievec_LDADD = $(ldadd)
sieve_test_LDADD = $(ldadd)

sievec_DEPENDENCIES = $(libs)
sieve_test_DEPENDENCIES = $(libs)

sievec_SOURCES = \
	sievec.c 

sieve_test_SOURCES = \
	sieve_test.c

