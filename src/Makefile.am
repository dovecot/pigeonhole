

sieve_subdirs = \
	lib-sieve \
	lib-sievestorage \
	plugins \
	lib-sieve-tool \
	sieve-tools \
	testsuite

if BUILD_MANAGESIEVE
managesieve_subdirs = \
	lib-managesieve \
	managesieve \
	managesieve-login
else
managesieve_subdirs =
endif

SUBDIRS = \
	$(sieve_subdirs) \
	$(managesieve_subdirs)
