noinst_LTLIBRARIES = libsieve_ext_environment.la

AM_CPPFLAGS = \
	-I../../ \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage 

tests = \
	tst-environment.c

libsieve_ext_environment_la_SOURCES = \
	$(tests) \
	ext-environment-common.c \
	ext-environment.c

public_headers = \
	sieve-ext-environment.h

headers = \
	ext-environment-common.h

if INSTALL_HEADERS
  pkginc_libdir=$(pkgincludedir)/sieve
  pkginc_lib_HEADERS = $(public_headers)
  noinst_HEADERS = $(headers)
else
  noinst_HEADERS = $(headers) $(public_headers)
endif

