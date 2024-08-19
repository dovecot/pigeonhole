

sieve_subdirs = \
	lib-sieve \
	lib-managesieve \
	plugins \
	lib-sieve-tool \
	sieve-tools \
	testsuite

if BUILD_MANAGESIEVE
managesieve_subdirs = \
	managesieve \
	managesieve-login
else
managesieve_subdirs =
endif

SUBDIRS = \
	$(sieve_subdirs) \
	$(managesieve_subdirs)
