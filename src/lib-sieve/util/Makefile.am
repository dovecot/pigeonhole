noinst_LTLIBRARIES = libsieve_util.la

AM_CPPFLAGS = \
	$(LIBDOVECOT_INCLUDE) \
	$(LIBDOVECOT_SERVICE_INCLUDE) \
	-DMODULEDIR=\""$(dovecot_moduledir)"\"

libsieve_util_la_DEPENDENCIES = $(LIBDOVECOT_STORAGE_DEPS) $(LIBDOVECOT_DEPS)

libsieve_util_la_SOURCES = \
	edit-mail.c \
	rfc2822.c \
	program-client-local.c \
	program-client-remote.c \
	program-client.c \
	realpath.c

headers = \
	edit-mail.h \
	rfc2822.h \
	program-client-private.h \
	program-client.h \
	realpath.h

pkginc_libdir=$(dovecot_pkgincludedir)/sieve
pkginc_lib_HEADERS = $(headers)
