#ifndef IMAP_FILTER_SIEVE_H
#define IMAP_FILTER_SIEVE_H

#include "sieve.h"
#include "imap-filter.h"

struct imap_filter_sieve_script;
struct imap_filter_sieve_context;

enum imap_filter_sieve_type {
	IMAP_FILTER_SIEVE_TYPE_DELIVERY,
	IMAP_FILTER_SIEVE_TYPE_PERSONAL,
	IMAP_FILTER_SIEVE_TYPE_GLOBAL,
	IMAP_FILTER_SIEVE_TYPE_SCRIPT,
};

struct imap_filter_sieve_context {
	pool_t pool;

	struct imap_filter_context *filter_context;
	enum imap_filter_sieve_type filter_type;

	struct mail_user *user;

	struct sieve_script *user_script;
	struct imap_filter_sieve_script *scripts;
	unsigned int scripts_count;

	struct mail *mail;

	struct sieve_script_env scriptenv;
	struct sieve_trace_config trace_config;
	struct sieve_trace_log *trace_log;

	string_t *errors;

	bool warnings:1;
	bool trace_log_initialized:1;
};

/*
 * FILTER Command
 */

bool cmd_filter_sieve(struct client_command_context *cmd);

/*
 * Context
 */

struct imap_filter_sieve_context *
imap_filter_sieve_context_create(struct imap_filter_context *ctx,
				 enum imap_filter_sieve_type type);
void imap_filter_sieve_context_free(struct imap_filter_sieve_context **_sctx);

/*
 * Compile
 */

int imap_filter_sieve_compile(struct imap_filter_sieve_context *sctx,
			      string_t **errors_r, bool *have_warnings_r);

/*
 * Open
 */

void imap_filter_sieve_open_input(struct imap_filter_sieve_context *sctx,
				  struct istream *input);
int imap_filter_sieve_open_personal(struct imap_filter_sieve_context *sctx,
				    const char *name,
				    enum mail_error *error_code_r,
				    const char **error_r) ATTR_NULL(2);
int imap_filter_sieve_open_global(struct imap_filter_sieve_context *sctx,
				    const char *name,
				    enum mail_error *error_code_r,
				    const char **error_r);

/*
 * Run
 */

int imap_sieve_filter_run_init(struct imap_filter_sieve_context *sctx);
int imap_sieve_filter_run_mail(struct imap_filter_sieve_context *sctx,
			       struct mail *mail, string_t **errors_r,
			       bool *have_warnings_r, bool *have_changes_r);

/*
 *
 */

void imap_filter_sieve_client_created(struct client *client);

void imap_filter_sieve_init(struct module *module);
void imap_filter_sieve_deinit(void);

#endif
