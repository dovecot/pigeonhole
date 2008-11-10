noinst_LTLIBRARIES = libsieve_ext_enotify.la

AM_CPPFLAGS = \
	-I../../ \
	-I$(dovecot_incdir) \
	-I$(dovecot_incdir)/src/lib \
	-I$(dovecot_incdir)/src/lib-mail \
	-I$(dovecot_incdir)/src/lib-storage 

commands = \
	cmd-notify.c

tests = \
	tst-valid-notify-method.c \
	tst-notify-method-capability.c

libsieve_ext_enotify_la_SOURCES = \
	ext-enotify.c \
	$(commands) \
	$(tests)

noinst_HEADERS = \
	ext-enotify-common.h

