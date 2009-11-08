noinst_LTLIBRARIES = libsieve_ext_enotify.la

AM_CPPFLAGS = \
	-I../../ \
	-I../variables \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage 

commands = \
	cmd-notify.c

tests = \
	tst-valid-notify-method.c \
	tst-notify-method-capability.c

var_modifiers = \
	vmodf-encodeurl.c

notify_methods = \
	ntfy-mailto.c

libsieve_ext_enotify_la_SOURCES = \
	ext-enotify.c \
	ext-enotify-common.c \
	$(commands) \
	$(tests) \
	$(var_modifiers) \
	$(notify_methods)

public_headers = \
	sieve-ext-enotify.h

headers = \
	ext-enotify-limits.h \
	ext-enotify-common.h

if INSTALL_HEADERS
  pkginc_libdir=$(dovecot_pkgincludedir)/sieve
  pkginc_lib_HEADERS = $(public_headers)
  noinst_HEADERS = $(headers)
else
  noinst_HEADERS = $(headers) $(public_headers)
endif
