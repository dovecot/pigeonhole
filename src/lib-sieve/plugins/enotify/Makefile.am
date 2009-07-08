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
	ext-notify.c \
	ext-enotify-common.c \
	$(commands) \
	$(tests) \
	$(var_modifiers) \
	$(notify_methods)

noinst_HEADERS = \
	sieve-ext-enotify.h \
	ext-enotify-limits.h \
	ext-enotify-common.h

