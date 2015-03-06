AM_CPPFLAGS = \
	-I$(top_srcdir)/src/lib-sieve \
	$(LIBDOVECOT_INCLUDE) \
	$(LIBDOVECOT_STORAGE_INCLUDE) \
	$(LIBDOVECOT_DOVEADM_INCLUDE)

doveadm_moduledir = $(dovecot_moduledir)/doveadm
lib10_doveadm_sieve_plugin_la_LDFLAGS = -module -avoid-version

doveadm_module_LTLIBRARIES = lib10_doveadm_sieve_plugin.la

lib10_doveadm_sieve_plugin_la_LIBADD = \
	$(top_builddir)/src/lib-sieve/libdovecot-sieve.la

commands = \
	doveadm-sieve-cmd-list.c \
	doveadm-sieve-cmd-get.c \
	doveadm-sieve-cmd-put.c \
	doveadm-sieve-cmd-delete.c \
	doveadm-sieve-cmd-activate.c \
	doveadm-sieve-cmd-rename.c

lib10_doveadm_sieve_plugin_la_SOURCES = \
	$(commands) \
	doveadm-sieve-cmd.c \
	doveadm-sieve-sync.c \
	doveadm-sieve-plugin.c

noinst_HEADERS = \
	doveadm-sieve-cmd.h \
	doveadm-sieve-plugin.h
