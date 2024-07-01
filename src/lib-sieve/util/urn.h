#ifndef URN_H
#define URN_H

struct urn {
	const char *assigned_name;
	const char *nid;
	const char *nss;

	const char *enc_r_component;
	const char *enc_q_component;
	const char *enc_f_component;
};

/*
 * URN parsing
 */

enum urn_parse_flags {
	/* Scheme part 'urn:' is already parsed externally. */
	URN_PARSE_SCHEME_EXTERNAL       = 0x01,
};

int urn_parse(const char *urn, enum urn_parse_flags flags, pool_t pool,
	      struct urn **urn_r, const char **error_r);
int urn_validate(const char *urn, enum urn_parse_flags flags,
		 const char **error_r);

/*
 * URN construction
 */

const char *urn_create(const struct urn *urn);

/*
 * URN equality
 */

int urn_normalize(const char *urn_in, enum urn_parse_flags flags,
		  const char **urn_out, const char **error_r);
int urn_equals(const char *urn1, const char *urn2, enum urn_parse_flags flags,
	       const char **error_r);

#endif
