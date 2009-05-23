pkglibexecdir = $(libexecdir)/dovecot

SUBDIRS = debug

bin_PROGRAMS = sievec sieved sieve-test # sieve-filter

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/lib-sieve \
	-I$(top_srcdir)/src/lib-sieve-tool \
	-I./debug \
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

sievec_LDFLAGS = -export-dynamic 
sieved_LDFLAGS = -export-dynamic 
sieve_test_LDFLAGS = -export-dynamic
# sieve_filter_LDFLAGS = -export-dynamic

libs = \
	$(top_srcdir)/src/lib-sieve/libsieve.la \
	$(top_srcdir)/src/lib-sieve-tool/libsieve-tool.la \
	$(dovecot_incdir)/src/lib-storage/libdovecot-storage.la \
	$(dovecot_incdir)/src/lib-dovecot/libdovecot.la \
	./debug/libsieve_ext_debug.la


sievec_LDADD = $(libs)
sieved_LDADD = $(libs)
sieve_test_LDADD = $(libs)
# sieve_filter_LDADD = $(libs)

sievec_DEPENDENCIES = $(libs)
sieved_DEPENDENCIES = $(libs)
sieve_test_DEPENDENCIES = $(libs)
# sieve_filter_DEPENDENCIES = $(libs)

sievec_SOURCES = \
	sievec.c 

sieved_SOURCES = \
	sieved.c 

sieve_test_SOURCES = \
	sieve-test.c 

# sieve_filter_SOURCES = \
# 	sieve-filter.c 

noinst_HEADERS =
