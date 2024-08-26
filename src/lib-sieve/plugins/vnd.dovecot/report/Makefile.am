noinst_LTLIBRARIES = libsieve_ext_vnd_report.la

AM_CPPFLAGS = \
	-I$(srcdir)/../../.. \
	-I$(srcdir)/../../../util \
	$(LIBDOVECOT_INCLUDE)

commands = \
	cmd-report.c

libsieve_ext_vnd_report_la_SOURCES = \
	ext-vnd-report.c \
	ext-vnd-report-settings.c \
	ext-vnd-report-common.c \
	$(commands)

noinst_HEADERS = \
	ext-vnd-report-settings.h \
	ext-vnd-report-common.h
