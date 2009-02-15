pkglibexecdir = $(libexecdir)/dovecot

bin_PROGRAMS = sievec sieved sieve-test sieve-filter

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/lib-sieve \
	-I$(top_srcdir)/src/lib-sieve-tool \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-index \
	-I$(dovecot_incdir)/src/lib-storage \
	-I$(dovecot_incdir)/src/deliver

#sievec_LDFLAGS = -export-dynamic -Wl,--start-group 
#sieved_LDFLAGS = -export-dynamic -Wl,--start-group 
#sieve_test_LDFLAGS = -export-dynamic -Wl,--start-group
#sieve_filter_LDFLAGS = -export-dynamic -Wl,--start-group

libs = \
	$(top_srcdir)/src/lib-sieve/libsieve.la \
	$(top_srcdir)/src/lib-sieve-tool/libsieve-tool.la \
	$(dovecot_incdir)/src/lib-storage/list/libstorage_list.a \
	$(dovecot_incdir)/src/lib-storage/register/libstorage-register.a \
	$(STORAGE_LIBS) 

ldadd = \
	$(libs) \
 	$(LIBICONV) \
	$(RAND_LIBS) \
	$(MODULE_LIBS)

sievec_LDADD = $(ldadd)
sieved_LDADD = $(ldadd)
sieve_test_LDADD = $(ldadd)
sieve_filter_LDADD = $(ldadd)

sievec_DEPENDENCIES = $(libs)
sieved_DEPENDENCIES = $(libs)
sieve_test_DEPENDENCIES = $(libs)
sieve_filter_DEPENDENCIES = $(libs)

sievec_SOURCES = \
	sievec.c 

sieved_SOURCES = \
	sieved.c 

sieve_test_SOURCES = \
	sieve-test.c 

sieve_filter_SOURCES = \
	sieve-filter.c 

noinst_HEADERS =
