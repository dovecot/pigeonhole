settingsdir = $(dovecot_moduledir)/settings

AM_CPPFLAGS = \
	$(LIBDOVECOT_INCLUDE)

libpigeonhole_settings_la_LDFLAGS = -module -avoid-version

settings_LTLIBRARIES = \
	libpigeonhole_settings.la

libpigeonhole_settings_la_SOURCES = \
	pigeonhole-settings.c
