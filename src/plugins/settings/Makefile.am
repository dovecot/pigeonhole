settingsdir = $(dovecot_moduledir)/settings

AM_CPPFLAGS = \
	$(LIBDOVECOT_INCLUDE) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/lib-sieve \
	-I$(top_srcdir)/src/lib-sieve/util \
	-I$(top_srcdir)/src/lib-sieve/storage/ldap \
	-I$(top_srcdir)/src/lib-managesieve \
	-I$(top_srcdir)/src/plugins/sieve-extprograms \
	-DSETTINGS_PLUGIN

libpigeonhole_settings_la_LDFLAGS = -module -avoid-version

settings_LTLIBRARIES = \
	libpigeonhole_settings.la

libpigeonhole_settings_la_SOURCES = \
	pigeonhole-settings-dynamic.c \
	pigeonhole-settings.c

# When LDAP is configured as a plugin, link the tiny settings-only
# convenience so the always-loaded settings plugin exports the
# sieve_ldap*_setting_parser_info symbols that the LDAP storage plugin
# (loaded later via dlopen) and doveconf both need. The convenience
# has no link-time dependency on libldap or libdovecot-ldap.
# In built-in mode the symbols already live in libdovecot-sieve.so;
# linking them here too would create a duplicate strong def.
# In no-LDAP mode there are no LDAP setting symbols at all.
if LDAP_PLUGIN
ldap_settings_la = \
	$(top_builddir)/src/lib-sieve/storage/ldap/libsieve_storage_ldap_settings.la
endif

libpigeonhole_settings_la_LIBADD = \
	$(top_builddir)/src/lib-sieve/libdovecot-sieve.la \
	$(top_builddir)/src/lib-managesieve/libdovecot-managesieve.la \
	$(ldap_settings_la)
libpigeonhole_settings_la_DEPENDENCIES = \
	$(top_builddir)/src/lib-sieve/libdovecot-sieve.la \
	$(top_builddir)/src/lib-managesieve/libdovecot-managesieve.la \
	$(ldap_settings_la)

noinst_HEADERS = \
	pigeonhole-settings.h

if DOVECOT_INSTALLED
SETTINGS_HISTORY_PY = $(dovecot_pkglibexecdir)/settings-history.py
else
SETTINGS_HISTORY_PY = $(abs_dovecotdir)/src/lib-settings/settings-history.py
endif

pigeonhole-settings.c: settings-history-pigeonhole.h
BUILT_SOURCES = settings-history-pigeonhole.h
settings-history-pigeonhole.h: $(srcdir)/settings-history-pigeonhole.txt $(SETTINGS_HISTORY_PY)
	$(AM_V_GEN)$(PYTHON) $(SETTINGS_HISTORY_PY) --pro 1 --plugin pigeonhole $< $@

pigeonhole-settings-dynamic.c: $(SETTING_FILES) $(SETTING_LINKED_FILES) $(top_srcdir)/src/plugins/settings/settings-get.pl
	$(AM_V_GEN)$(top_srcdir)/src/plugins/settings/settings-get.pl \
		$(SETTING_FILES) -- $(SETTING_LINKED_FILES) > pigeonhole-settings-dynamic.c || \
		rm -f pigeonhole-settings-dynamic.c

EXTRA_DIST = \
	settings-history-pigeonhole.txt \
	settings-history-pigeonhole.h \
	settings-get.pl
