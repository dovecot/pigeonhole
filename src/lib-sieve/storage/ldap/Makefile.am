noinst_LTLIBRARIES = libsieve_storage_ldap.la

sieve_plugindir = $(dovecot_moduledir)/sieve
sieve_plugin_LTLIBRARIES =

AM_CPPFLAGS = \
	$(LIBDOVECOT_INCLUDE) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/lib-sieve

ldap_sources = \
	sieve-ldap-db.c \
	sieve-ldap-script.c \
	sieve-ldap-storage.c \
	sieve-ldap-storage-settings.c

libsieve_storage_ldap_la_SOURCES = $(ldap_sources)
libsieve_storage_ldap_la_LIBADD = $(LDAP_LIBS)

noinst_HEADERS = \
	sieve-ldap-db.h \
	sieve-ldap-storage-settings.h \
	sieve-ldap-storage.h

if LDAP_PLUGIN
sieve_plugin_LTLIBRARIES += lib10_sieve_storage_ldap_plugin.la

lib10_sieve_storage_ldap_plugin_la_LDFLAGS = -module -avoid-version
lib10_sieve_storage_ldap_plugin_la_LIBADD = $(LDAP_LIBS)
lib10_sieve_storage_ldap_plugin_la_DEPENDENCIES = $(LDAP_LIBS)
lib10_sieve_storage_ldap_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) -DPLUGIN_BUILD
lib10_sieve_storage_ldap_plugin_la_SOURCES = $(ldap_sources)
endif
