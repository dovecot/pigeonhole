noinst_LTLIBRARIES = libsievestorage.la

AM_CPPFLAGS = \
	$(LIBDOVECOT_INCLUDE) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/lib-sieve

libsievestorage_la_SOURCES = \
	sieve-storage-save.c \
	sieve-storage-script.c \
	sieve-storage-list.c \
	sieve-storage-quota.c \
	sieve-storage.c

noinst_HEADERS = \
	sieve-storage-save.h \
	sieve-storage-script.h \
	sieve-storage-list.h \
	sieve-storage-quota.h \
	sieve-storage-private.h \
	sieve-storage.h
