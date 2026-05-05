#ifndef SIEVE_RUSAGE_H
#define SIEVE_RUSAGE_H

#include "sieve-common.h"

struct sieve_storage;
struct mail_user;

/* Per-user resource usage file lives at <INBOX-namespace-root>/sieve-rusage.
   It survives script renames, deletions, and re-uploads under a different
   name, preventing CPU-budget bypass. Tracking is enabled only for personal
   user-controlled storage. */

void sieve_rusage_storage_init(struct sieve_storage *storage,
			       struct mail_user *user);

/* Load persisted resource usage from the per-user file. Returns 1 on success,
   0 if the storage has no rusage file (no INBOX namespace path), -1 on error.
   Applies the resource_usage_timeout decay. flags_r receives sieve binary
   header flags (RESOURCE_LIMIT) that were persisted. */
int sieve_rusage_storage_load(struct sieve_storage *storage,
			      struct sieve_resource_usage *rusage_r,
			      uint32_t *flags_r);

/* Atomically add delta_rusage to and OR flags_to_set into the persisted
   per-user rusage file. Read-modify-write under fcntl WRLCK. Returns 1 on
   success, 0 if no rusage file is configured, -1 on error. */
int sieve_rusage_storage_add(struct sieve_storage *storage,
			     const struct sieve_resource_usage *delta_rusage,
			     uint32_t flags_to_set);

#endif
