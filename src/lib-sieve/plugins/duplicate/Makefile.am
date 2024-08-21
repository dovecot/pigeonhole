noinst_LTLIBRARIES = libsieve_ext_duplicate.la

AM_CPPFLAGS = \
	-I$(srcdir)/../.. \
	$(LIBDOVECOT_INCLUDE)

tests = \
	tst-duplicate.c

extensions = \
	ext-duplicate.c

libsieve_ext_duplicate_la_SOURCES = \
	$(tests) \
	$(extensions) \
	ext-duplicate-settings.c \
	ext-duplicate-common.c

noinst_HEADERS = \
	ext-duplicate-settings.h \
	ext-duplicate-common.h

