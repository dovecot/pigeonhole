#ifndef SIEVE_ADDRESS_SOURCE_H
#define SIEVE_ADDRESS_SOURCE_H

struct sieve_instance;
struct sieve_script_env;
struct sieve_message_context;
enum sieve_execute_flags;

enum sieve_address_source_type {
	SIEVE_ADDRESS_SOURCE_DEFAULT = 0,
	SIEVE_ADDRESS_SOURCE_SENDER,
	SIEVE_ADDRESS_SOURCE_RECIPIENT,
	SIEVE_ADDRESS_SOURCE_ORIG_RECIPIENT,
	SIEVE_ADDRESS_SOURCE_USER_EMAIL,
	SIEVE_ADDRESS_SOURCE_POSTMASTER,
	SIEVE_ADDRESS_SOURCE_EXPLICIT
};

struct sieve_address_source {
	enum sieve_address_source_type type;
	const struct smtp_address *address;
};

bool sieve_address_source_parse(pool_t pool, const char *value,
				struct sieve_address_source *asrc);

int sieve_address_source_get_address(struct sieve_address_source *asrc,
				     struct sieve_instance *svinst,
				     const struct sieve_script_env *senv,
				     struct sieve_message_context *msgctx,
				     enum sieve_execute_flags flags,
				     const struct smtp_address **addr_r);

#endif
