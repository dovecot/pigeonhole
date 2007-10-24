sbin_PROGRAMS = sievec

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

libs = \
	$(top_srcdir)/src/lib-sieve/libsieve.a \
	$(dovecot_incdir)/src/lib/liblib.a

sievec_LDADD = \
	$(libs) \
	$(plugins) \
 	$(LIBICONV) \
    $(RAND_LIBS) \
    $(MODULE_LIBS)

sievec_DEPENDENCIES = $(libs)

sievec_SOURCES = \
	sievec.c 

