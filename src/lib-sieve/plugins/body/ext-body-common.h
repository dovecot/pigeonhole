#ifndef __EXT_BODY_COMMON_H
#define __EXT_BODY_COMMON_H

extern int ext_body_my_id;
extern const struct sieve_extension body_extension;

struct ext_body_part {
	const char *content;
	unsigned long size;
};

bool ext_body_get_content
(const struct sieve_runtime_env *renv, const char * const *content_types,
	int decode_to_plain, struct ext_body_part **parts_r);

#endif /* __EXT_BODY_COMMON_H */
