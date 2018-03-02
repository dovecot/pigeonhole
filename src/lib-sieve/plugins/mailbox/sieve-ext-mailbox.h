#ifndef SIEVE_EXT_MAILBOX_H
#define SIEVE_EXT_MAILBOX_H

/* sieve_ext_mailbox_get_extension():
 *   Get the extension struct for the mailbox extension.
 */
static inline const struct sieve_extension *sieve_ext_mailbox_get_extension
(struct sieve_instance *svinst)
{
	return sieve_extension_get_by_name(svinst, "mailbox");
}

/* sieve_ext_mailbox_register_create_tag():
 *   Register the :create tagged argument for a command other than fileinto and
 *   redirect.
 */
void sieve_ext_mailbox_register_create_tag
	(struct sieve_validator *valdtr, const struct sieve_extension *mailbox_ext,
		const char *command);

#endif
