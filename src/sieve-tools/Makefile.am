SUBDIRS = debug

bin_PROGRAMS = sievec sieved sieve-test

if BUILD_UNFINISHED
bin_PROGRAMS += sieve-filter
endif

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/lib-sieve \
	-I$(top_srcdir)/src/lib-sieve-tool \
	-I./debug \
	$(LIBDOVECOT_INCLUDE) \
	$(LIBDOVECOT_SERVICE_INCLUDE)
	$(LIBDOVECOT_STORAGE_INCLUDE)

libs = \
	$(top_srcdir)/src/lib-sieve/libdovecot-sieve.la \
	$(top_srcdir)/src/lib-sieve-tool/libsieve-tool.la \
	$(LIBDOVECOT_STORAGE) \
	$(LIBDOVECOT) \
	./debug/libsieve_ext_debug.la

# Sieve Compile Tool

sievec_LDFLAGS = -export-dynamic
sievec_LDADD = $(libs)
sievec_DEPENDENCIES = $(libs)

sievec_SOURCES = \
	sievec.c 

# Sieve Dump Tool

sieved_LDFLAGS = -export-dynamic
sieved_LDADD = $(libs)
sieved_DEPENDENCIES = $(libs)

sieved_SOURCES = \
	sieved.c 

# Sieve Test Tool

sieve_test_LDFLAGS = -export-dynamic
sieve_test_LDADD = $(libs)
sieve_test_DEPENDENCIES = $(libs)

sieve_test_SOURCES = \
	sieve-test.c 

## Unfinished tools

# Sieve Filter Tool

sieve_filter_LDFLAGS = -export-dynamic
sieve_filter_LDADD = $(libs)
sieve_filter_DEPENDENCIES = $(libs)

sieve_filter_SOURCES = \
	sieve-filter.c 

noinst_HEADERS =
