AM_CPPFLAGS = \
	-I$(top_srcdir)/src/lib-sieve \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-dict \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage \
	-I$(dovecot_incdir)/src/deliver

lda_moduledir = $(moduledir)/lda

lib90_sieve_plugin_la_LDFLAGS = -module -avoid-version

lda_module_LTLIBRARIES = lib90_sieve_plugin.la

lib90_sieve_plugin_la_LIBADD = \
	$(top_srcdir)/src/lib-sieve/libsieve.la

lib90_sieve_plugin_la_SOURCES = \
	lda-sieve-plugin.c 

noinst_HEADERS = \
	lda-sieve-plugin.h
