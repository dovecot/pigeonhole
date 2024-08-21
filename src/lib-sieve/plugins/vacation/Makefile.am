noinst_LTLIBRARIES = libsieve_ext_vacation.la

AM_CPPFLAGS = \
	-I$(srcdir)/../.. \
	-I$(srcdir)/../../util \
	$(LIBDOVECOT_INCLUDE)

cmds = \
	cmd-vacation.c

libsieve_ext_vacation_la_SOURCES = \
	$(cmds) \
	ext-vacation-settings.c \
	ext-vacation-common.c \
	ext-vacation.c \
	ext-vacation-seconds.c

noinst_HEADERS = \
	ext-vacation-settings.h \
	ext-vacation-common.h

