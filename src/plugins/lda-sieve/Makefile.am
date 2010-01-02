AM_CPPFLAGS = \
	-I$(top_srcdir)/src/lib-sieve \
	$(SIEVE_LDA_DOVECOT_INCLUDE)

lib90_sieve_plugin_la_LDFLAGS = -module -avoid-version

module_LTLIBRARIES = lib90_sieve_plugin.la

lib90_sieve_plugin_la_LIBADD = \
	$(top_srcdir)/src/lib-sieve/libdovecot-sieve.la

lib90_sieve_plugin_la_SOURCES = \
	lda-sieve-log.c \
	lda-sieve-plugin.c 

noinst_HEADERS = \
	lda-sieve-log.h \
	lda-sieve-plugin.h
