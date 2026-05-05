/* Copyright (c) 2026 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "strnum.h"
#include "ioloop.h"
#include "eacces-error.h"
#include "file-dotlock.h"
#include "mail-namespace.h"
#include "mail-storage.h"
#include "mailbox-list.h"

#include "sieve-common.h"
#include "sieve-settings.h"
#include "sieve-storage-private.h"
#include "sieve-rusage.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#define SIEVE_RUSAGE_FILENAME "sieve-rusage"
#define SIEVE_RUSAGE_VERSION_TAG "V1"
#define SIEVE_RUSAGE_LOCK_TIMEOUT 10
#define SIEVE_RUSAGE_LOCK_STALE_TIMEOUT 60

/* Maximum size of the on-disk file. Single-line ASCII format with four
   space-separated tokens; anything larger is treated as corrupt. */
#define SIEVE_RUSAGE_MAX_FILE_SIZE 128

struct sieve_rusage_record {
	uint32_t flags;
	uint32_t cpu_time_msecs;
	time_t update_time;
};

void sieve_rusage_storage_init(struct sieve_storage *storage,
			       struct mail_user *user)
{
	struct mail_namespace *ns;
	const char *root;

	if (!storage->is_personal || user == NULL)
		return;

	ns = mail_namespace_find_inbox(user->namespaces);
	if (ns == NULL)
		return;

	if (!mailbox_list_get_root_path(ns->list, MAILBOX_LIST_PATH_TYPE_DIR,
					&root) || root == NULL)
		return;

	storage->user = user;
	storage->inbox_list = ns->list;
	storage->rusage_path = p_strconcat(storage->pool, root, "/",
					   SIEVE_RUSAGE_FILENAME, NULL);
}

static bool
sieve_rusage_parse(const char *line, struct sieve_rusage_record *rec)
{
	const char *const *tokens;
	uint32_t cpu_secs;
	bool ok = FALSE;

	T_BEGIN {
		tokens = t_strsplit_spaces(line, " \t");
		if (tokens[0] == NULL ||
		    strcmp(tokens[0], SIEVE_RUSAGE_VERSION_TAG) != 0)
			break;
		if (tokens[1] == NULL || tokens[2] == NULL ||
		    tokens[3] == NULL || tokens[4] != NULL)
			break;
		if (str_to_uint32(tokens[1], &rec->flags) < 0)
			break;
		if (str_to_uint32(tokens[2], &cpu_secs) < 0)
			break;
		if (str_to_time(tokens[3], &rec->update_time) < 0)
			break;
		if (cpu_secs > UINT32_MAX / 1000)
			rec->cpu_time_msecs = UINT32_MAX;
		else
			rec->cpu_time_msecs = cpu_secs * 1000;
		ok = TRUE;
	} T_END;

	return ok;
}

static int
sieve_rusage_read(struct sieve_storage *storage,
		  struct sieve_rusage_record *rec_r)
{
	char buf[SIEVE_RUSAGE_MAX_FILE_SIZE + 1];
	char *nl;
	ssize_t ret;
	int fd;

	i_zero(rec_r);

	fd = open(storage->rusage_path, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT)
			return 0;
		e_error(storage->event, "open(%s) failed: %m",
			storage->rusage_path);
		return -1;
	}

	ret = read(fd, buf, sizeof(buf) - 1);
	i_close_fd(&fd);
	if (ret < 0) {
		e_error(storage->event, "read(%s) failed: %m",
			storage->rusage_path);
		return -1;
	}
	if (ret == 0)
		return 0;
	if (ret >= (ssize_t)(sizeof(buf) - 1)) {
		e_warning(storage->event, "%s: file too large; resetting",
			  storage->rusage_path);
		return 0;
	}
	buf[ret] = '\0';

	nl = strchr(buf, '\n');
	if (nl != NULL)
		*nl = '\0';

	if (!sieve_rusage_parse(buf, rec_r)) {
		e_warning(storage->event, "%s: malformed content; resetting",
			  storage->rusage_path);
		i_zero(rec_r);
	}
	return 0;
}

static void
sieve_rusage_dotlock_settings(struct dotlock_settings *set_r)
{
	i_zero(set_r);
	set_r->timeout = SIEVE_RUSAGE_LOCK_TIMEOUT;
	set_r->stale_timeout = SIEVE_RUSAGE_LOCK_STALE_TIMEOUT;
	set_r->use_excl_lock = TRUE;
}

int sieve_rusage_storage_load(struct sieve_storage *storage,
			      struct sieve_resource_usage *rusage_r,
			      uint32_t *flags_r)
{
	struct sieve_rusage_record rec;
	unsigned int timeout;

	sieve_resource_usage_init(rusage_r);
	*flags_r = 0;

	if (storage->rusage_path == NULL)
		return 0;

	if (sieve_rusage_read(storage, &rec) < 0)
		return -1;

	timeout = storage->svinst->set->resource_usage_timeout;
	if (rec.update_time != 0 &&
	    (ioloop_time - rec.update_time) > (time_t)timeout) {
		/* decayed */
		return 1;
	}

	rusage_r->cpu_time_msecs = rec.cpu_time_msecs;
	*flags_r = rec.flags;
	return 1;
}

int sieve_rusage_storage_add(struct sieve_storage *storage,
			     const struct sieve_resource_usage *delta_rusage,
			     uint32_t flags_to_set)
{
	struct sieve_rusage_record rec;
	struct dotlock_settings dlset;
	struct dotlock *dotlock;
	unsigned int timeout;
	uint32_t old_cpu, cpu_secs;
	string_t *out;
	ssize_t wret;
	int fd;

	if (storage->rusage_path == NULL)
		return 0;

	sieve_rusage_dotlock_settings(&dlset);

	/* The namespace root may not exist yet on the very first update for
	   a user (e.g. LDA refused execution before any mail was delivered).
	   Create it on demand so file_dotlock_open does not fail with ENOENT. */
	if (storage->inbox_list != NULL &&
	    mailbox_list_mkdir_root(storage->inbox_list, NULL,
				    MAILBOX_LIST_PATH_TYPE_DIR) < 0)
		return -1;

	fd = file_dotlock_open_mode(&dlset, storage->rusage_path, 0,
				    0600, (uid_t)-1, (gid_t)-1, &dotlock);
	if (fd == -1) {
		if (errno == EACCES) {
			e_error(storage->event, "%s",
				eacces_error_get_creating("file_dotlock_open",
							  storage->rusage_path));
		} else {
			e_error(storage->event,
				"file_dotlock_open(%s) failed: %m",
				storage->rusage_path);
		}
		return -1;
	}

	if (sieve_rusage_read(storage, &rec) < 0) {
		file_dotlock_delete(&dotlock);
		return -1;
	}

	timeout = storage->svinst->set->resource_usage_timeout;
	if (rec.update_time != 0 &&
	    (ioloop_time - rec.update_time) > (time_t)timeout) {
		/* decayed: discard previous values */
		rec.cpu_time_msecs = 0;
		rec.flags = 0;
	}

	old_cpu = rec.cpu_time_msecs;
	if ((UINT32_MAX - old_cpu) < delta_rusage->cpu_time_msecs)
		rec.cpu_time_msecs = UINT32_MAX;
	else
		rec.cpu_time_msecs = old_cpu + delta_rusage->cpu_time_msecs;
	rec.flags |= flags_to_set;
	rec.update_time = ioloop_time;

	/* Round CPU time up to whole seconds for persistence. */
	if (rec.cpu_time_msecs >= UINT32_MAX - 999)
		cpu_secs = UINT32_MAX / 1000;
	else
		cpu_secs = (rec.cpu_time_msecs + 999) / 1000;

	out = t_str_new(64);
	str_printfa(out, "%s %u %u %lld\n", SIEVE_RUSAGE_VERSION_TAG,
		    rec.flags, cpu_secs, (long long)rec.update_time);

	wret = write(fd, str_data(out), str_len(out));
	if (wret != (ssize_t)str_len(out)) {
		e_error(storage->event,
			"write(%s) failed: %m (wrote %zd/%zu)",
			storage->rusage_path, wret, str_len(out));
		file_dotlock_delete(&dotlock);
		return -1;
	}

	if (file_dotlock_replace(&dotlock, 0) < 0) {
		e_error(storage->event,
			"file_dotlock_replace(%s) failed: %m",
			storage->rusage_path);
		return -1;
	}
	return 1;
}
