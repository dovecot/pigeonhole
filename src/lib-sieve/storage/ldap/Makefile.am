sieve_plugindir = $(dovecot_moduledir)/sieve
sieve_plugin_LTLIBRARIES =
noinst_LTLIBRARIES =

AM_CPPFLAGS = \
	$(LDAP_CFLAGS) \
	$(LIBDOVECOT_INCLUDE) \
	$(LIBDOVECOT_LDAP_INCLUDE) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/lib-sieve

ldap_runtime_sources = \
	sieve-ldap-db.c \
	sieve-ldap-script.c \
	sieve-ldap-storage.c \
	sieve-ldap-storage-settings.c

noinst_HEADERS = \
	sieve-ldap-db.h \
	sieve-ldap-storage-settings.h \
	sieve-ldap-storage.h

if LDAP_STORAGE
# Built-in LDAP: storage runtime plus its setting_parser_info definitions
# get linked directly into libdovecot-sieve via lib-sieve/Makefile.am.
noinst_LTLIBRARIES += libsieve_storage_ldap.la
libsieve_storage_ldap_la_SOURCES = \
	$(ldap_runtime_sources)
libsieve_storage_ldap_la_LIBADD = \
	$(LDAP_LIBS) \
	$(LIBDOVECOT_LDAP) \
	$(DOVECOT_LDAP_LIBS)
endif

if LDAP_PLUGIN
# Plugin LDAP: the setting_parser_info definitions live in a tiny
# convenience that the always-loaded libpigeonhole_settings plugin links
# against. It has NO link-time dependency on libldap or libdovecot-ldap
# because sieve-ldap-storage-settings.c only references LDAP_DEREF_* /
# LDAP_SCOPE_* preprocessor macros, no libldap functions.
noinst_LTLIBRARIES += libsieve_storage_ldap_settings.la
libsieve_storage_ldap_settings_la_SOURCES = sieve-ldap-storage-settings.c

# The actual plugin .so contains only the runtime. References to
# sieve_ldap*_setting_parser_info resolve at dlopen time to the strong
# defs that libpigeonhole_settings.so already exports.
sieve_plugin_LTLIBRARIES += lib10_sieve_storage_ldap_plugin.la
lib10_sieve_storage_ldap_plugin_la_LDFLAGS = -module -avoid-version
lib10_sieve_storage_ldap_plugin_la_LIBADD = \
	libsieve_storage_ldap_settings.la \
	$(LIBDOVECOT) \
	$(LIBDOVECOT_LDAP) \
	$(DOVECOT_LDAP_LIBS) \
	$(LDAP_LIBS)
lib10_sieve_storage_ldap_plugin_la_DEPENDENCIES = libsieve_storage_ldap_settings.la $(LIBDOVECOT_LDAP_DEPS)
lib10_sieve_storage_ldap_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) -DPLUGIN_BUILD
lib10_sieve_storage_ldap_plugin_la_SOURCES = $(ldap_runtime_sources)
endif
