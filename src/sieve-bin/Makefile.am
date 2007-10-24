bin_PROGRAMS = sievec

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/lib-sieve \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage 

plugin_dir = \
	$(top_srcdir)/src/lib-sieve/plugins

plugins = \
	$(plugin_dir)/vacation/lib_ext_vacation.a

sievec_LDFLAGS = -export-dynamic
sieve_test_LDFLAGS = -export-dynamic

libs = \
	$(top_srcdir)/src/lib-sieve/libsieve.a \
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

