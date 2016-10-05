noinst_LTLIBRARIES = libsieve_util.la

AM_CPPFLAGS = \
	$(LIBDOVECOT_INCLUDE) \
	$(LIBDOVECOT_SERVICE_INCLUDE) \
	-DMODULEDIR=\""$(dovecot_moduledir)"\"

libsieve_util_la_DEPENDENCIES = $(LIBDOVECOT_STORAGE_DEPS) $(LIBDOVECOT_DEPS)

libsieve_util_la_SOURCES = \
	edit-mail.c \
	rfc2822.c \
	realpath.c \
	strtrim.c

headers = \
	edit-mail.h \
	rfc2822.h \
	realpath.h \
	strtrim.h

pkginc_libdir=$(dovecot_pkgincludedir)/sieve
pkginc_lib_HEADERS = $(headers)
