noinst_LTLIBRARIES = libsieve_storage_file.la

AM_CPPFLAGS = \
	$(LIBDOVECOT_INCLUDE) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/lib-sieve \
	-I$(top_srcdir)/src/lib-sieve/util

libsieve_storage_file_la_SOURCES = \
	sieve-file-script.c \
	sieve-file-script-sequence.c \
	sieve-file-storage-settings.c \
	sieve-file-storage-active.c \
	sieve-file-storage-save.c \
	sieve-file-storage-list.c \
	sieve-file-storage-quota.c \
	sieve-file-storage.c

noinst_HEADERS = \
	sieve-file-storage-settings.h \
	sieve-file-storage.h
