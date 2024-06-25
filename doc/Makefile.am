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

BUILT_SOURCES = pigeonhole-man

MAN_URL=https://doc.dovecot.org/assets/$(PIGEONHOLE_ASSET_VERSION)/pigeonhole.txz

AM_V_Q = $(am__v_Q_$(V))
am__v_Q_ = $(am__v_Q_$(AM_DEFAULT_VERBOSITY))
am__v_Q_0 = -q
am__v_Q_1 =

pigeonhole-man:
	$(AM_V_at)$(WGET) -nv -O- $(MAN_URL) | xz -d | $(am__untar) \
		&& touch pigeonhole-man || rm -f pigeonhole-man

all-local:
	$(AM_V_at)if [ ! -e man/doveadm-sieve.1 ]; then \
		rm -f pigeonhole-man; \
		$(MAKE) pigeonhole-man; \
	fi

EXTRA_DIST = \
	devel \
	pigeonhole-man \
	$(docfiles)
