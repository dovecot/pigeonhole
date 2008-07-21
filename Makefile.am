SUBDIRS = src

EXTRA_DIST = \
	sieve/* \
	doc/* \
    COPYING.LGPL \
	DESIGN \
    ChangeLog

if MAINTAINER_MODE
ChangeLog: .hg/dirstate
	hg log --style=changelog > ChangeLog
endif

test:
	$(MAKE) -C src/testsuite test
