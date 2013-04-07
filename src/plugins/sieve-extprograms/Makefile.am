sieve_plugindir = $(moduledir)/sieve

sieve_plugin_LTLIBRARIES = lib90_sieve_extprograms_plugin.la

lib90_sieve_extprograms_plugin_la_LDFLAGS = -module -avoid-version

AM_CPPFLAGS = \
    -I$(top_srcdir)/src/lib-sieve \
	-I$(top_srcdir)/src/lib-sieve/plugins/copy \
	-I$(top_srcdir)/src/lib-sieve/plugins/variables \
	$(LIBDOVECOT_INCLUDE) \
	-DPKG_RUNDIR=\""$(rundir)"\"

commands = \
	cmd-pipe.c \
	cmd-filter.c \
	cmd-execute.c

extensions = \
	ext-pipe.c \
	ext-filter.c \
	ext-execute.c

lib90_sieve_extprograms_plugin_la_SOURCES = \
	script-client-local.c \
	script-client-remote.c \
	script-client.c \
	$(commands) \
	$(extensions) \
	sieve-extprograms-common.c \
	sieve-extprograms-plugin.c

noinst_HEADERS = \
	script-client.h \
	script-client-private.h \
	sieve-extprograms-common.h \
	sieve-extprograms-plugin.h

