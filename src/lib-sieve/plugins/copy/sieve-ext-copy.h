#ifndef SIEVE_EXT_COPY_H
#define SIEVE_EXT_COPY_H

extern const struct sieve_extension_def copy_extension;

/* sieve_ext_copy_get_extension():
 *   Get the extension struct for the copy extension.
 */
static inline int
sieve_ext_copy_get_extension(struct sieve_instance *svinst,
			     const struct sieve_extension **ext_r)
{
	return sieve_extension_register(svinst, &copy_extension, FALSE, ext_r);
}

/* sieve_ext_copy_register_tag():
 *   Register the :copy tagged argument for a command other than fileinto and
 *   redirect.
 */
void sieve_ext_copy_register_tag
	(struct sieve_validator *valdtr, const struct sieve_extension *copy_ext,
		const char *command);

#endif
