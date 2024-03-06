settingsdir = $(dovecot_moduledir)/settings

AM_CPPFLAGS = \
	$(LIBDOVECOT_INCLUDE) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/lib-sieve \
	-I$(top_srcdir)/src/lib-sieve/storage/ldap \
	-I$(top_srcdir)/src/lib-managesieve \
	-DSETTINGS_PLUGIN
if LDAP_PLUGIN
AM_CPPFLAGS += -DPLUGIN_BUILD -DLDAP_PLUGIN
endif

libpigeonhole_settings_la_LDFLAGS = -module -avoid-version

settings_LTLIBRARIES = \
	libpigeonhole_settings.la

libpigeonhole_settings_la_SOURCES = \
	pigeonhole-settings-dynamic.c \
	pigeonhole-settings.c
libpigeonhole_settings_la_LIBADD = \
	$(top_builddir)/src/lib-sieve/libdovecot-sieve.la \
	$(top_builddir)/src/lib-managesieve/libdovecot-managesieve.la \
	$(LDAP_LIBS)
libpigeonhole_settings_la_DEPENDENCIES = \
	$(top_builddir)/src/lib-sieve/libdovecot-sieve.la \
	$(top_builddir)/src/lib-managesieve/libdovecot-managesieve.la \
	$(LDAP_LIBS)

noinst_HEADERS = \
	pigeonhole-settings.h

pigeonhole-settings-dynamic.c: $(SETTING_FILES) $(SETTING_LINKED_FILES) $(top_srcdir)/src/plugins/settings/settings-get.pl
	$(AM_V_GEN)$(top_srcdir)/src/plugins/settings/settings-get.pl \
		$(SETTING_FILES) -- $(SETTING_LINKED_FILES) > pigeonhole-settings-dynamic.c || \
		rm -f pigeonhole-settings-dynamic.c

EXTRA_DIST = \
	settings-get.pl
