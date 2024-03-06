#ifndef IMAP_SIEVE_H
#define IMAP_SIEVE_H

#include "sieve-storage.h"

struct client;

/*
 * IMAP event
 */

struct imap_sieve_event {
	struct mailbox *dest_mailbox, *src_mailbox;
	const char *cause;
	const char *changed_flags;
};

struct imap_sieve_context {
	struct imap_sieve_event event;
	struct mail *mail;

	struct imap_sieve *isieve;
};

static inline bool
imap_sieve_event_cause_valid(const char *cause)
{
	return (strcasecmp(cause, "APPEND") == 0 ||
		strcasecmp(cause, "COPY") == 0 ||
		strcasecmp(cause, "FLAG") == 0);
}

/*
 * IMAP Sieve
 */

struct imap_sieve;

struct imap_sieve *imap_sieve_init(struct client *client);
void imap_sieve_deinit(struct imap_sieve **_isieve);

/*
 * IMAP Sieve run
 */

struct imap_sieve_run;

int imap_sieve_run_init(struct imap_sieve *isieve,
			struct event *dest_mbox_event,
			struct mailbox *dest_mailbox,
			struct mailbox *src_mailbox,
			const char *cause, const char *script_name,
			const char *before_type, const char *after_type,
			struct imap_sieve_run **isrun_r);

int imap_sieve_run_mail(struct imap_sieve_run *isrun, struct mail *mail,
			const char *changed_flags, bool *fatal_r);

void imap_sieve_run_deinit(struct imap_sieve_run **_isrun);

#endif
