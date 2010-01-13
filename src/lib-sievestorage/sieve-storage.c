/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "home-expand.h"
#include "ioloop.h"
#include "mkdir-parents.h"

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

#define SIEVE_DEFAULT_PATH "~/.dovecot.sieve"

#define MAX_DIR_CREATE_MODE 0770

#define CRITICAL_MSG \
  "Internal error occured. Refer to server log for more information."
#define CRITICAL_MSG_STAMP CRITICAL_MSG " [%Y-%m-%d %H:%M:%S]"

static void sieve_storage_verror
	(struct sieve_error_handler *ehandler ATTR_UNUSED, 
		const char *location ATTR_UNUSED, const char *fmt, va_list args);

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

static int sieve_storage_verify_dir
(const char *path, mode_t *mode_r, gid_t *gid_r)
{
	struct stat st;

	if ( stat(path, &st) < 0 ) {
		const char *p;
		int ret;

		if ( errno != ENOENT )
			return -1;

		/* Ascend to parent path element */
		p = strrchr(path, '/');

		/* Path components exhausted? */
		if  (p == NULL || p == path )
			return -1;

		/* Recurse */
		T_BEGIN {
			ret = sieve_storage_verify_dir(t_strdup_until(path, p), mode_r, gid_r);
		} T_END;

		if ( ret < 0 )
			return -1;

		if ( mkdir_chown(path, *mode_r, (uid_t) -1, *gid_r) < 0 )
			return -1;

		return 0;
	}
	
	/* Report back permission bits and group id back to caller */

	if ( !S_ISDIR(st.st_mode) ) {
		i_error("sieve-storage: Path is not a directory: %s", path);
		return -1;
	}

	*mode_r = st.st_mode & MAX_DIR_CREATE_MODE & 0777;

	/* Check whether changing GID will be necessary */
	if ( (st.st_mode & S_ISGID) != 0 ) {
		/* Setgid bit set */
		*gid_r = (gid_t) -1;
	} else if ( getegid() == st.st_gid ) {
		*gid_r = (gid_t) -1;
	} else {
		*gid_r = st.st_gid;
	}

	return 0;
}

static struct sieve_storage *_sieve_storage_create
(struct sieve_instance *svinst, const char *user, const char *home, bool debug)
{
	pool_t pool;
	struct sieve_storage *storage;
	const char *tmp_dir, *link_path;
	const char *sieve_data, *active_path, *active_fname, *storage_dir;
	mode_t dir_mode;
	gid_t dir_gid;
	unsigned long long int uint_setting;
	size_t size_setting;

	/* 
	 * Read settings 
	 */

	active_path = sieve_setting_get(svinst, "sieve");
    sieve_data = sieve_setting_get(svinst, "sieve_dir");

	if ( sieve_data == NULL )
		sieve_data = sieve_setting_get(svinst, "sieve_storage");

	/* Get path to active Sieve script */

	if ( active_path != NULL ) {
		if ( *active_path == '\0' ) {
			/* disabled */
			if ( debug ) 
				i_debug("sieve-storage: sieve is disabled (sieve = \"\")");
			return NULL;
		}
	} else {

		if ( home == NULL ) {
			/* we must have a home directory */
			i_error("sieve-storage: userdb(%s) didn't return a home directory or "
				"sieve script location", user);
			return NULL;
		}

		active_path = SIEVE_DEFAULT_PATH;
	}

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

	/* Find out where to put the script storage */

	storage_dir = NULL;

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

				storage_dir = home_expand_tilde("~/sieve", home);
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
		}

		if (access("/sieve", R_OK|W_OK|X_OK) == 0) {
			storage_dir = "/sieve";
			if ( debug )
				i_debug("sieve-storage: /sieve exists, assuming chroot");
		}
	} else {
		storage_dir = sieve_data;
	}

	if (storage_dir == NULL || *storage_dir == '\0') {
		if ( debug )
			i_debug("sieve-storage: couldn't find storage dir");
		return NULL;
	}

	/* Expand home directories in path */
	active_path = home_expand_tilde(active_path, home);
	storage_dir = home_expand_tilde(storage_dir, home);

	if ( debug ) {
		i_debug("sieve-storage: "
			"using active sieve script path: %s", active_path);
 		i_debug("sieve-storage: "
			"using sieve script storage directory: %s", storage_dir); 
	}

	/* 
	 * Ensure sieve local directory structure exists (full autocreate):
	 *  This currently currently only consists of a ./tmp direcory
	 */
	tmp_dir = t_strconcat( storage_dir, "/tmp", NULL );	
	if ( sieve_storage_verify_dir(tmp_dir, &dir_mode, &dir_gid) < 0 ) {
		i_error("sieve-storage: sieve_storage_verify_dir(%s) failed: %m", tmp_dir);
		return NULL;
	}

	/* 
	 * Create storage object 
	 */

	pool = pool_alloconly_create("sieve-storage", 512+256);
	storage = p_new(pool, struct sieve_storage, 1);
	storage->svinst = svinst;
	storage->debug = debug;
	storage->pool = pool;
	storage->dir = p_strdup(pool, storage_dir);
	storage->user = p_strdup(pool, user);
	storage->active_path = p_strdup(pool, active_path);
	storage->active_fname = p_strdup(pool, active_fname);

	storage->dir_create_mode = dir_mode;
	storage->file_create_mode = dir_mode & 0666;
	storage->dir_create_gid = dir_gid;

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
			i_info("sieve-storage: quota: storage limit: %llu bytes", 
				(unsigned long long int) storage->max_storage);
		}
		if ( storage->max_scripts > 0 ) {
			i_info("sieve-storage: quota: script count limit: %llu scripts", 
				(unsigned long long int) storage->max_scripts);
		}
	}

	return storage;
}

struct sieve_storage *sieve_storage_create
(struct sieve_instance *svinst, const char *user, const char *home, bool debug)
{
	struct sieve_storage *storage;

	T_BEGIN {
		storage = _sieve_storage_create(svinst, user, home, debug);
	} T_END;

	return storage;
}


void sieve_storage_free(struct sieve_storage *storage)
{
	sieve_error_handler_unref(&storage->ehandler);

	pool_unref(&storage->pool);
}

/* Error handling */

struct sieve_error_handler *sieve_storage_get_error_handler
(struct sieve_storage *storage)
{
	struct sieve_storage_ehandler *ehandler;

	if ( storage->ehandler == NULL ) {
		pool_t pool = pool_alloconly_create("sieve_storage_ehandler", 512);
		ehandler = p_new(pool, struct sieve_storage_ehandler,1);
		sieve_error_handler_init(&ehandler->handler, pool, 1);

		ehandler->handler.verror = sieve_storage_verror;
		ehandler->storage = storage;
		
		storage->ehandler = (struct sieve_error_handler *) ehandler;
	}

	return storage->ehandler;
}

static void sieve_storage_verror
(struct sieve_error_handler *ehandler, const char *location ATTR_UNUSED,
    const char *fmt, va_list args)
{
	struct sieve_storage_ehandler *sehandler = 
		(struct sieve_storage_ehandler *) ehandler; 
	struct sieve_storage *storage = sehandler->storage;

	sieve_storage_clear_error(storage);
	
	if (fmt != NULL) {
		storage->error = i_strdup_vprintf(fmt, args);
	}
}

void sieve_storage_clear_error(struct sieve_storage *storage)
{
	i_free(storage->error);
	storage->error = NULL;
}

void sieve_storage_set_error
(struct sieve_storage *storage, enum sieve_storage_error error,
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

void sieve_storage_set_internal_error(struct sieve_storage *storage)
{
	struct tm *tm;
	char str[256];

	tm = localtime(&ioloop_time);

	i_free(storage->error);
	storage->error_code = SIEVE_STORAGE_ERROR_TEMP;
	storage->error =
	  strftime(str, sizeof(str), CRITICAL_MSG_STAMP, tm) > 0 ?
	  i_strdup(str) : i_strdup(CRITICAL_MSG);
}

void sieve_storage_set_critical(struct sieve_storage *storage,
             const char *fmt, ...)
{
	va_list va;
	
	sieve_storage_clear_error(storage);
	if (fmt != NULL) {
		va_start(va, fmt);
		i_error("sieve-storage: %s", t_strdup_vprintf(fmt, va));
		va_end(va);
		
		/* critical errors may contain sensitive data, so let user
		   see only "Internal error" with a timestamp to make it
		   easier to look from log files the actual error message. */
		sieve_storage_set_internal_error(storage);
	}
}

const char *sieve_storage_get_last_error
	(struct sieve_storage *storage, enum sieve_storage_error *error_r)
{
	/* We get here only in error situations, so we have to return some
	   error. If storage->error is NULL, it means we forgot to set it at
	   some point.. 
	 */
  
	if ( error_r != NULL ) 
		*error_r = storage->error_code;

	return storage->error != NULL ? storage->error : "Unknown error";
}


