noinst_LTLIBRARIES = libsieve_ext_special_use.la

AM_CPPFLAGS = \
	-I$(srcdir)/../.. \
	$(LIBDOVECOT_INCLUDE)

tags = \
	tag-specialuse.c

tests = \
	tst-specialuse-exists.c

libsieve_ext_special_use_la_SOURCES = \
	$(tags) \
	$(tests) \
	ext-special-use-common.c \
	ext-special-use.c

headers = \
	ext-special-use-common.h

noinst_HEADERS = $(headers)
