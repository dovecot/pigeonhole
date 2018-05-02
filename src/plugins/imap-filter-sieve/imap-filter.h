#ifndef IMAP_FILTER_H
#define IMAP_FILTER_H

struct mail_duplicate_db;

struct sieve_script;
struct sieve_storage;
struct sieve_binary;

struct imap_filter_context {
	struct client_command_context *cmd;
	struct mailbox *box;
	struct mailbox_transaction_context *trans;
	struct mail_search_context *search_ctx;

	struct imap_parser *parser;

	struct imap_filter_sieve_context *sieve;
	const char *script_name;
	uoff_t script_len;
	struct istream *script_input;

	struct mail_search_args *sargs;

	struct timeout *to;

	bool failed:1;
	bool compile_failure:1;
	bool have_seqsets:1;
	bool have_modseqs:1;
};

bool imap_filter_search(struct client_command_context *cmd);

int imap_filter_deinit(struct imap_filter_context *ctx);

void imap_filter_context_free(struct imap_filter_context *ctx);

/* Commands */

bool cmd_filter(struct client_command_context *cmd);

#endif
