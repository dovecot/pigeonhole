/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "home-expand.h"
#include "ioloop.h"
#include "mkdir-parents.h"
#include "eacces-error.h"
#include "unlink-old-files.h"
#include "mail-storage-private.h"

#include "sieve.h"
#include "sieve-common.h"
#include "sieve-settings.h"
#include "sieve-error-private.h"
#include "sieve-settings.h"

#include "sieve-storage-private.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>
#include <time.h>
#include <utime.h>

#define SIEVE_DEFAULT_PATH "~/.dovecot."SIEVE_SCRIPT_FILEEXT

#define MAX_DIR_CREATE_MODE 0770

#define CRITICAL_MSG \
  "Internal error occured. Refer to server log for more information."
#define CRITICAL_MSG_STAMP CRITICAL_MSG " [%Y-%m-%d %H:%M:%S]"

static void sieve_storage_verror
	(struct sieve_error_handler *ehandler ATTR_UNUSED,
		unsigned int flags ATTR_UNUSED, const char *location ATTR_UNUSED,
		const char *fmt, va_list args);

static const char *sieve_storage_get_relative_link_path
	(const char *active_path, const char *storage_dir)
{
	const char *link_path, *p;
	size_t pathlen;

	/* Determine to what extent the sieve storage and active script
	 * paths match up. This enables the managed symlink to be short and the
	 * sieve storages can be moved around without trouble (if the active
	 * script path is common to the script storage).
	 */
	p = strrchr(active_path, '/');
	if ( p == NULL ) {
		link_path = storage_dir;
	} else {
		pathlen = p - active_path;

		if ( strncmp( active_path, storage_dir, pathlen ) == 0 &&
			(storage_dir[pathlen] == '/' || storage_dir[pathlen] == '\0') )
		{
			if ( storage_dir[pathlen] == '\0' )
				link_path = "";
			else
				link_path = storage_dir + pathlen + 1;
		} else
			link_path = storage_dir;
	}

	/* Add trailing '/' when link path is not empty
	 */
	pathlen = strlen(link_path);
    if ( pathlen != 0 && link_path[pathlen-1] != '/')
        return t_strconcat(link_path, "/", NULL);

	return t_strdup(link_path);
}

static mode_t get_dir_mode(mode_t mode)
{
	/* Add the execute bit if either read or write bit is set */

	if ((mode & 0600) != 0) mode |= 0100;
	if ((mode & 0060) != 0) mode |= 0010;
	if ((mode & 0006) != 0) mode |= 0001;

	return mode;
}

static void sieve_storage_get_permissions
(const char *path, mode_t *file_mode_r, mode_t *dir_mode_r, gid_t *gid_r,
	const char **gid_origin_r, bool debug)
{
	struct stat st;

	/* Use safe defaults */
	*file_mode_r = 0600;
	*dir_mode_r = 0700;
	*gid_r = (gid_t)-1;
	*gid_origin_r = "defaults";

	if ( stat(path, &st) < 0 ) {
		if ( !ENOTFOUND(errno) ) {
			i_error("sieve-storage: stat(%s) failed: %m", path);
		} else if ( debug ) {
			i_debug("sieve-storage: permission lookup failed from %s", path);
		}
		return;

	} else {
		*file_mode_r = (st.st_mode & 0666) | 0600;
		*dir_mode_r = (st.st_mode & 0777) | 0700;
		*gid_origin_r = path;

		if ( !S_ISDIR(st.st_mode) ) {
			/* We're getting permissions from a file. Apply +x modes as necessary. */
			*dir_mode_r = get_dir_mode(*dir_mode_r);
		}

		if (S_ISDIR(st.st_mode) && (st.st_mode & S_ISGID) != 0) {
			/* Directory's GID is used automatically for new files */
			*gid_r = (gid_t)-1;
		} else if ((st.st_mode & 0070) >> 3 == (st.st_mode & 0007)) {
			/* Group has same permissions as world, so don't bother changing it */
			*gid_r = (gid_t)-1;
		} else if (getegid() == st.st_gid) {
			/* Using our own gid, no need to change it */
			*gid_r = (gid_t)-1;
		} else {
			*gid_r = st.st_gid;
		}
	}

	if ( debug ) {
		i_debug("sieve-storage: using permissions from %s: mode=0%o gid=%ld",
			path, (int)*dir_mode_r, *gid_r == (gid_t)-1 ? -1L : (long)*gid_r);
	}
}

static int mkdir_verify
(const char *dir, mode_t mode, gid_t gid, const char *gid_origin, bool debug)
{
	struct stat st;

	if ( stat(dir, &st) == 0 )
		return 0;

	if ( errno == EACCES ) {
		i_error("sieve-storage: mkdir_verify: %s", eacces_error_get("stat", dir));
		return -1;
	} else if ( errno != ENOENT ) {
		i_error("sieve-storage: mkdir_verify: stat(%s) failed: %m", dir);
		return -1;
	}

	if ( mkdir_parents_chgrp(dir, mode, gid, gid_origin) == 0 ) {
		if ( debug )
			i_debug("sieve-storage: created storage directory %s", dir);
		return 0;
	}

	switch ( errno ) {
	case EEXIST:
		return 0;
	case ENOENT:
		i_error("sieve-storage: storage was deleted while it was being created");
		break;
	case EACCES:
		i_error("sieve-storage: %s",
			eacces_error_get_creating("mkdir_parents_chgrp", dir));
		break;
	default:
		i_error("sieve-storage: mkdir_parents_chgrp(%s) failed: %m", dir);
		break;
	}

	return -1;
}

static int check_tmp(const char *path)
{
	struct stat st;

	/* If tmp/ directory exists, we need to clean it up once in a while */
	if ( stat(path, &st) < 0 ) {
		if ( errno == ENOENT )
			return 0;
		if ( errno == EACCES ) {
			i_error("sieve-storage: check_tmp: %s", eacces_error_get("stat", path));
			return -1;
		}
		i_error("sieve-storage: check_tmp: stat(%s) failed: %m", path);
		return -1;
	}

	if ( st.st_atime > st.st_ctime + SIEVE_STORAGE_TMP_DELETE_SECS ) {
		/* The directory should be empty. we won't do anything
		   until ctime changes. */
	} else if ( st.st_atime < ioloop_time - SIEVE_STORAGE_TMP_SCAN_SECS ) {
		/* Time to scan */
		(void)unlink_old_files(path, "",
			ioloop_time - SIEVE_STORAGE_TMP_DELETE_SECS);
	}
	return 1;
}

static int _sieve_storage_open_inbox
(struct mail_user *user, struct mailbox **box_r)
{
	struct mail_namespace *ns;
	struct mailbox *box;
	enum mailbox_flags flags = MAILBOX_FLAG_IGNORE_ACLS;
	enum mail_error error;

	ns = mail_namespace_find_inbox(user->namespaces);
	*box_r = box = mailbox_alloc(ns->list, "INBOX", flags);
	if (mailbox_open(box) == 0)
		return 0;

	i_warning("sieve-storage: "
		"Failed to open user INBOX for attribute modifications: %s",
		mailbox_get_last_error(box, &error));
	return -1;
}

static struct sieve_storage *_sieve_storage_create
(struct sieve_instance *svinst, struct mail_user *user, const char *home,
	enum sieve_storage_flags flags)
{
	pool_t pool;
	struct sieve_storage *storage;
	struct mailbox *inbox = NULL;
	bool debug = ( (flags & SIEVE_STORAGE_FLAG_DEBUG) != 0 );
	const char *tmp_dir, *link_path, *path;
	const char *sieve_data, *active_path, *active_fname, *storage_dir;
	const char *username = user->username;
	mode_t dir_create_mode, file_create_mode;
	gid_t file_create_gid;
	const char *file_create_gid_origin;
	unsigned long long int uint_setting;
	size_t size_setting;
	int ret;

	/*
	 * Configure active script path
	 */

	active_path = sieve_setting_get(svinst, "sieve");

	/* Get path to active Sieve script */

	if ( active_path != NULL ) {
		const char *p;

		if ( *active_path == '\0' ) {
			/* disabled */
			if ( debug )
				i_debug("sieve-storage: sieve is disabled (sieve=\"\")");
			return NULL;
		}

		/* Parse full location into a file path */
		p = strchr(active_path, ':');
		if ( p != NULL ) {
	 		if ( strncmp(active_path, "file", p-active_path) != 0 ) {
				i_error("sieve-storage: Cannot open non-file script location "
					"for active script `%s'",	active_path);
				return NULL;
			}
			active_path = p+1;

			p = strchr(active_path, ';');
			if ( p != NULL )
				active_path = t_strdup_until(active_path, p);
		}

	} else {
		if ( debug ) {
			i_debug("sieve-storage: sieve active script path is unconfigured; "
				"using default (sieve=%s)", SIEVE_DEFAULT_PATH);
		}

		active_path = SIEVE_DEFAULT_PATH;
	}

	/* Substitute home dir if necessary */

	path = home_expand_tilde(active_path, home);
	if ( path == NULL ) {
		i_error("sieve-storage: userdb(%s) didn't return a home directory "
			"for substitition in active script path (sieve=%s)",
			username, active_path);
		return NULL;
	}

	active_path = path;

	/* Get the filename for the active script link */

	active_fname = strrchr(active_path, '/');
	if ( active_fname == NULL )
		active_fname = active_path;
	else
		active_fname++;

	if ( *active_fname == '\0' ) {
		/* Link cannot be just a path */
		i_error("sieve-storage: "
			"path to active symlink must include the link's filename. Path is: %s",
			active_path);

		return NULL;
	}

	/*
	 * Configure script storage directory
	 */

	storage_dir = NULL;

	/* Read setting */

	sieve_data = sieve_setting_get(svinst, "sieve_dir");

	if ( sieve_data == NULL )
		sieve_data = sieve_setting_get(svinst, "sieve_storage");

	/* Determine location */

	if ( sieve_data == NULL || *sieve_data == '\0' ) {
		/* We'll need to figure out the storage location ourself.
		 *
		 * It's $HOME/sieve or /sieve when (presumed to be) chrooted.
		 */
		if ( home != NULL && *home != '\0' ) {
			if (access(home, R_OK|W_OK|X_OK) == 0) {
				/* Use default ~/sieve */

				if ( debug ) {
					i_debug("sieve-storage: root exists (%s)", home);
				}

				storage_dir = t_strconcat(home, "/sieve", NULL);
			} else {
				/* Don't have required access on the home directory */

				if ( debug ) {
					i_debug("sieve-storage: access(%s, rwx): "
						"failed: %m", home);
				}
			}
		} else {
			if ( debug )
				i_debug("sieve-storage: HOME not set");

			if (access("/sieve", R_OK|W_OK|X_OK) == 0) {
				storage_dir = "/sieve";
				if ( debug )
					i_debug("sieve-storage: /sieve exists, assuming chroot");
			}
		}
	} else {
		const char *p;

		/* Parse full location into a file path */
		p = strchr(sieve_data, ':');
		if ( p != NULL ) {
	 		if (strncmp(sieve_data, "file", p-sieve_data) != 0 ) {
				i_error("sieve-storage: Cannot open non-file script storage `%s'",
					sieve_data);
				return NULL;
			}
			sieve_data = p+1;

			p = strchr(sieve_data, ';');
			if ( p != NULL )
				sieve_data = t_strdup_until(sieve_data, p);
		}

		storage_dir = sieve_data;
	}

	if (storage_dir == NULL || *storage_dir == '\0') {
		i_error("sieve-storage: couldn't find storage root directory; "
			"sieve_dir was left unconfigured and autodetection failed");
		return NULL;
	}

	/* Expand home directory in path */

	path = home_expand_tilde(storage_dir, home);
	if ( path == NULL ) {
		i_error("sieve-storage: userdb(%s) didn't return a home directory "
			"for substitition in storage root directory (sieve_dir=%s)",
			username, storage_dir);
		return NULL;
	}

	storage_dir = path;

	if ( debug ) {
		i_debug("sieve-storage: "
			"using active sieve script path: %s", active_path);
 		i_debug("sieve-storage: "
			"using sieve script storage directory: %s", storage_dir);
	}

	/* get the storage mtime before we modify it ourself. FIXME: do this
	   later, only just before modifying the sieve dir */
	struct stat st;
	if (stat(storage_dir, &st) < 0) {
		if (errno != ENOENT) {
			i_error("stat(%s) failed: %m", storage_dir);
			return NULL;
		}
		st.st_mtime = 0;
	}

	/* Get permissions */

	sieve_storage_get_permissions
		(storage_dir, &file_create_mode, &dir_create_mode, &file_create_gid,
			&file_create_gid_origin, debug);

	/*
	 * Ensure sieve local directory structure exists (full autocreate):
	 *  This currently only consists of a ./tmp direcory
	 */

	tmp_dir = t_strconcat(storage_dir, "/tmp", NULL);

	/* Try to find and clean up tmp dir */
	if ( (ret=check_tmp(tmp_dir)) < 0 )
		return NULL;

	/* Auto-create if necessary */
	if ( ret == 0 && mkdir_verify(tmp_dir, dir_create_mode, file_create_gid,
		file_create_gid_origin, debug) < 0 )
		return NULL;

	/* Open user's INBOX for attribute updates if necessary */
	if ( (flags & SIEVE_STORAGE_FLAG_SYNCHRONIZING) == 0 )
		(void)_sieve_storage_open_inbox(user, &inbox);

	/*
	 * Create storage object
	 */

	pool = pool_alloconly_create("sieve-storage", 512+256);
	storage = p_new(pool, struct sieve_storage, 1);
	storage->svinst = svinst;
	storage->flags = flags;
	storage->pool = pool;
	storage->dir = p_strdup(pool, storage_dir);
	storage->username = p_strdup(pool, username);
	storage->active_path = p_strdup(pool, active_path);
	storage->active_fname = p_strdup(pool, active_fname);
	storage->prev_mtime = st.st_mtime;

	storage->dir_create_mode = dir_create_mode;
	storage->file_create_mode = file_create_mode;
	storage->file_create_gid = file_create_gid;

	storage->inbox = inbox;

	/* Get the path to be prefixed to the script name in the symlink pointing
	 * to the active script.
	 */
	link_path = sieve_storage_get_relative_link_path
		(storage->active_path, storage->dir);

	if ( debug )
		i_debug("sieve-storage: "
			"relative path to sieve storage in active link: %s", link_path);

	storage->link_path = p_strdup(pool, link_path);

	/* Get quota settings */

	storage->max_storage = 0;
	storage->max_scripts = 0;

	if ( sieve_setting_get_size_value
		(svinst, "sieve_quota_max_storage", &size_setting) ) {
		storage->max_storage = size_setting;
	}

	if ( sieve_setting_get_uint_value
		(svinst, "sieve_quota_max_scripts", &uint_setting) ) {
		storage->max_scripts = uint_setting;
	}

	if ( debug ) {
		if ( storage->max_storage > 0 ) {
			i_debug("sieve-storage: quota: storage limit: %llu bytes",
				(unsigned long long int) storage->max_storage);
		}
		if ( storage->max_scripts > 0 ) {
			i_debug("sieve-storage: quota: script count limit: %llu scripts",
				(unsigned long long int) storage->max_scripts);
		}
	}
	return storage;
}

struct sieve_storage *sieve_storage_create
(struct sieve_instance *svinst, struct mail_user *user, const char *home,
	enum sieve_storage_flags flags)
{
	struct sieve_storage *storage;

	T_BEGIN {
		storage = _sieve_storage_create(svinst, user, home, flags);
	} T_END;

	return storage;
}

void sieve_storage_free(struct sieve_storage *storage)
{
	if (storage->inbox != NULL)
		mailbox_free(&storage->inbox);
	sieve_error_handler_unref(&storage->ehandler);

	pool_unref(&storage->pool);
}

int sieve_storage_get_last_change
(struct sieve_storage *storage, time_t *last_change_r)
{
	*last_change_r = storage->prev_mtime;
	return 0;
}

void sieve_storage_set_modified
(struct sieve_storage *storage, time_t mtime)
{
	struct utimbuf times;
	time_t cur_mtime;

	if ( mtime != (time_t)-1 ) {
		if ( sieve_storage_get_last_change(storage, &cur_mtime) >= 0 &&
			cur_mtime > mtime )
			return;
	} else {
		mtime = ioloop_time;
	}

	times.actime = mtime;
	times.modtime = mtime;
	if ( utime(storage->dir, &times) < 0 ) {
		switch ( errno ) {
		case ENOENT:
			break;
		case EACCES:
			i_error("sieve-storage: %s", eacces_error_get("utime", storage->dir));
			break;
		default:
			i_error("sieve-storage: utime(%s) failed: %m", storage->dir);
		}
	} else {
		storage->prev_mtime = mtime;
	}
}

/* Error handling */

struct sieve_error_handler *sieve_storage_get_error_handler
(struct sieve_storage *storage)
{
	struct sieve_storage_ehandler *ehandler;

	if ( storage->ehandler == NULL ) {
		pool_t pool = pool_alloconly_create("sieve_storage_ehandler", 512);
		ehandler = p_new(pool, struct sieve_storage_ehandler,1);
		sieve_error_handler_init(&ehandler->handler, storage->svinst, pool, 1);

		ehandler->handler.verror = sieve_storage_verror;
		ehandler->storage = storage;

		storage->ehandler = (struct sieve_error_handler *) ehandler;
	}

	return storage->ehandler;
}

static void ATTR_FORMAT(4, 0) sieve_storage_verror
(struct sieve_error_handler *ehandler, unsigned int flags ATTR_UNUSED,
	const char *location ATTR_UNUSED, const char *fmt, va_list args)
{
	struct sieve_storage_ehandler *sehandler =
		(struct sieve_storage_ehandler *) ehandler;
	struct sieve_storage *storage = sehandler->storage;

	sieve_storage_clear_error(storage);

	if (fmt != NULL) {
		storage->error = i_strdup_vprintf(fmt, args);
	}
	storage->error_code = SIEVE_ERROR_TEMP_FAILURE;
}

void sieve_storage_clear_error(struct sieve_storage *storage)
{
	i_free(storage->error);
	storage->error_code = SIEVE_ERROR_NONE;
	storage->error = NULL;
}

void sieve_storage_set_error
(struct sieve_storage *storage, enum sieve_error error,
	const char *fmt, ...)
{
	va_list va;

	sieve_storage_clear_error(storage);

	if (fmt != NULL) {
		va_start(va, fmt);
		storage->error = i_strdup_vprintf(fmt, va);
		va_end(va);
	}

	storage->error_code = error;
}

void sieve_storage_set_critical
(struct sieve_storage *storage, const char *fmt, ...)
{
	va_list va;

	sieve_storage_clear_error(storage);
	if (fmt != NULL) {
		i_free(storage->error);
		storage->error_code = SIEVE_ERROR_TEMP_FAILURE;

		if ( (storage->flags & SIEVE_STORAGE_FLAG_SYNCHRONIZING) == 0 ) {
			struct tm *tm;
			char str[256];

			va_start(va, fmt);
			i_error("sieve-storage: %s", t_strdup_vprintf(fmt, va));
			va_end(va);

			/* critical errors may contain sensitive data, so let user
			   see only "Internal error" with a timestamp to make it
			   easier to look from log files the actual error message. */
			tm = localtime(&ioloop_time);
			storage->error =
				strftime(str, sizeof(str), CRITICAL_MSG_STAMP, tm) > 0 ?
				i_strdup(str) : i_strdup(CRITICAL_MSG);
		} else {
			/* no user is involved while synchronizing, so do it the
			   normal way */
			va_start(va, fmt);
			storage->error = i_strdup_vprintf(fmt, va);
			va_end(va);
		}
	}
}

const char *sieve_storage_get_last_error
(struct sieve_storage *storage, enum sieve_error *error_r)
{
	/* We get here only in error situations, so we have to return some
	   error. If storage->error is NULL, it means we forgot to set it at
	   some point..
	 */

	if ( error_r != NULL )
		*error_r = storage->error_code;

	return storage->error != NULL ? storage->error : "Unknown error";
}

/*
 * INBOX attributes
 */

static void sieve_storage_inbox_transaction_finish
(struct sieve_storage *storage, struct mailbox_transaction_context **t)
{
	struct mailbox *inbox = storage->inbox;

	if (mailbox_transaction_commit(t) < 0) {
		enum mail_error error;
		
		i_warning("sieve-storage: Failed to update INBOX attributes: %s",
			mail_storage_get_last_error(mailbox_get_storage(inbox), &error));
	}
}

void sieve_storage_inbox_script_attribute_set
(struct sieve_storage *storage, const char *name)
{
	struct mailbox_transaction_context *t;
	const char *key;

	if (storage->inbox == NULL)
		return;

	key = t_strconcat
		(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES, name, NULL);
	t = mailbox_transaction_begin(storage->inbox, 0);	
	mail_index_attribute_set(t->itrans, TRUE, key, ioloop_time, 0);
	sieve_storage_inbox_transaction_finish(storage, &t);
}

void sieve_storage_inbox_script_attribute_rename
(struct sieve_storage *storage, const char *oldname, const char *newname)
{
	struct mailbox_transaction_context *t;
	const char *oldkey, *newkey;

	if (storage->inbox == NULL)
		return;

	oldkey = t_strconcat
		(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES, oldname, NULL);
	newkey = t_strconcat
		(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES, newname, NULL);

	t = mailbox_transaction_begin(storage->inbox, 0);	
	mail_index_attribute_unset(t->itrans, TRUE, oldkey, ioloop_time);
	mail_index_attribute_set(t->itrans, TRUE, newkey, ioloop_time, 0);
	sieve_storage_inbox_transaction_finish(storage, &t);
}

void sieve_storage_inbox_script_attribute_unset
(struct sieve_storage *storage, const char *name)
{
	struct mailbox_transaction_context *t;
	const char *key;

	if (storage->inbox == NULL)
		return;

	key = t_strconcat
		(MAILBOX_ATTRIBUTE_PREFIX_SIEVE_FILES, name, NULL);
	
	t = mailbox_transaction_begin(storage->inbox, 0);	
	mail_index_attribute_unset(t->itrans, TRUE, key, ioloop_time);
	sieve_storage_inbox_transaction_finish(storage, &t);
}

