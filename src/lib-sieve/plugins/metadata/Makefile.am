noinst_LTLIBRARIES = libsieve_ext_metadata.la

libsieve_ext_metadata_la_LDFLAGS = -module -avoid-version

AM_CPPFLAGS = \
	-I$(srcdir)/../.. \
	-I$(srcdir)/../variables \
	$(LIBDOVECOT_INCLUDE) \
	$(LIBDOVECOT_STORAGE_INCLUDE)

tests = \
	tst-metadata.c \
	tst-metadataexists.c

extensions = \
	ext-metadata.c

libsieve_ext_metadata_la_SOURCES = \
	$(tests) \
	$(extensions)

noinst_HEADERS = \
	ext-metadata-common.h

