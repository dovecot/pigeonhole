#ifndef MANAGESIEVE_URL_H
#define MANAGESIEVE_URL_H

#include "net.h"
#include "uri-util.h"

#include "managesieve-protocol.h"

struct managesieve_url {
	/* server */
	struct uri_host host;
	in_port_t port;

	/* userinfo (not parsed by default) */
	const char *user;
	const char *password;

	/* path */
	const char *owner;
	const char *scriptname;
};

/*
 * Sieve URL parsing
 */

enum managesieve_url_parse_flags {
	/* Scheme part 'sieve:' is already parsed externally. This implies that
	   this is an absolute SIEVE URL. */
	MANAGESIEVE_URL_PARSE_SCHEME_EXTERNAL	= 0x01,
	/* Allow 'user:password@' part in SIEVE URL */
	MANAGESIEVE_URL_ALLOW_USERINFO_PART	= 0x04,
};

int managesieve_url_parse(const char *url,
			  enum managesieve_url_parse_flags flags, pool_t pool,
			  struct managesieve_url **url_r, const char **error_r)
			  ATTR_NULL(4);

/*
 * Sieve URL evaluation
 */

static inline in_port_t
managesieve_url_get_port_default(const struct managesieve_url *url,
				 in_port_t default_port)
{
	return (url->port != 0 ? url->port : default_port);
}

static inline in_port_t
managesieve_url_get_port(const struct managesieve_url *url)
{
	return managesieve_url_get_port_default(url, MANAGESIEVE_DEFAULT_PORT);
}

/*
 * Sieve URL manipulation
 */

void managesieve_url_init_authority_from(struct managesieve_url *dest,
					 const struct managesieve_url *src);
void managesieve_url_copy_authority(pool_t pool, struct managesieve_url *dest,
				    const struct managesieve_url *src);
struct managesieve_url *
managesieve_url_clone_authority(pool_t pool, const struct managesieve_url *src);

void managesieve_url_copy(pool_t pool, struct managesieve_url *dest,
			  const struct managesieve_url *src);
void managesieve_url_copy_with_userinfo(pool_t pool,
					struct managesieve_url *dest,
					const struct managesieve_url *src);

struct managesieve_url *
managesieve_url_clone(pool_t pool,const struct managesieve_url *src);
struct managesieve_url *
managesieve_url_clone_with_userinfo(pool_t pool,
				    const struct managesieve_url *src);

/*
 * Sieve URL construction
 */

const char *managesieve_url_create(const struct managesieve_url *url);

const char *managesieve_url_create_host(const struct managesieve_url *url);
const char *managesieve_url_create_authority(const struct managesieve_url *url);

#endif
