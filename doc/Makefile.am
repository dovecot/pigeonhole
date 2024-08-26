SUBDIRS = \
	man \
	example-config \
	rfc \
	extensions \
	locations \
	plugins

docfiles =

if BUILD_DOCS
sieve_doc_DATA = $(docfiles)
endif

BUILT_SOURCES = stamp-man

MAN_URL=https://$(PIGEONHOLE_ASSET_URL)/$(PIGEONHOLE_ASSET_VERSION)/pigeonhole.txz

AM_V_Q = $(am__v_Q_$(V))
am__v_Q_ = $(am__v_Q_$(AM_DEFAULT_VERBOSITY))
am__v_Q_0 = -q
am__v_Q_1 =

stamp-man:
	$(AM_V_at)$(WGET) -nv -O- $(MAN_URL) | xz -d | $(am__untar) \
		&& touch stamp-man || rm -f stamp-man

all-local:
	$(AM_V_at)if [ ! -e man/doveadm-sieve.1 ]; then \
		rm -f stamp-man; \
		$(MAKE) stamp-man; \
	fi

EXTRA_DIST = \
	devel \
	$(docfiles) \
	stamp-man
