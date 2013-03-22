AM_CPPFLAGS = \
	-I$(top_srcdir)/src/lib-sieve \
	-I$(top_srcdir)/src/lib-sievestorage \
	$(LIBDOVECOT_INCLUDE)

doveadm_moduledir = $(dovecot_moduledir)/doveadm
lib10_doveadm_sieve_plugin_la_LDFLAGS = -module -avoid-version

doveadm_module_LTLIBRARIES = lib10_doveadm_sieve_plugin.la

lib10_doveadm_sieve_plugin_la_SOURCES = \
	doveadm-sieve-plugin.c
lib10_doveadm_sieve_plugin_la_LIBADD = \
	$(top_builddir)/src/lib-sieve/libdovecot-sieve.la \
	$(top_builddir)/src/lib-sievestorage/libsievestorage.la
