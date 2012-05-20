noinst_LTLIBRARIES = libsieve_ext_duplicate.la

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/lib-sieve \
	$(LIBDOVECOT_INCLUDE)

tests = \
	tst-duplicate.c

extensions = \
	ext-duplicate.c

libsieve_ext_duplicate_la_SOURCES = \
	$(tests) \
	$(extensions) \
	ext-duplicate-common.c

noinst_HEADERS = \
	ext-duplicate-common.h

