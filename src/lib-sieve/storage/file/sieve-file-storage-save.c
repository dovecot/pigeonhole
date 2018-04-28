/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "hostpid.h"
#include "ioloop.h"
#include "array.h"
#include "buffer.h"
#include "istream.h"
#include "ostream.h"
#include "str.h"
#include "eacces-error.h"
#include "safe-mkstemp.h"

#include "sieve-file-storage.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <utime.h>

struct sieve_file_save_context {
	struct sieve_storage_save_context context;

	pool_t pool;

	struct ostream *output;
	int fd;
	const char *tmp_path;

	time_t mtime;

	bool failed:1;
	bool finished:1;
};

static const char *sieve_generate_tmp_filename(const char *scriptname)
{
	static struct timeval last_tv = { 0, 0 };
	struct timeval tv;

	/* use secs + usecs to guarantee uniqueness within this process. */
	if (ioloop_timeval.tv_sec > last_tv.tv_sec ||
		(ioloop_timeval.tv_sec == last_tv.tv_sec &&
		ioloop_timeval.tv_usec > last_tv.tv_usec)) {
		tv = ioloop_timeval;
	} else {
		tv = last_tv;
		if (++tv.tv_usec == 1000000) {
			tv.tv_sec++;
			tv.tv_usec = 0;
		}
	}
	last_tv = tv;

	if ( scriptname == NULL ) {
		return t_strdup_printf("%s.M%sP%s.%s.tmp",
			dec2str(tv.tv_sec), dec2str(tv.tv_usec),
			my_pid, my_hostname);
	}

	scriptname = t_strdup_printf("%s_%s.M%sP%s.%s",
		scriptname,	dec2str(tv.tv_sec), dec2str(tv.tv_usec),
		my_pid, my_hostname);
	return sieve_script_file_from_name(scriptname);
}

static int sieve_file_storage_create_tmp
(struct sieve_file_storage *fstorage, const char *scriptname,
	const char **fpath_r)
{
	struct sieve_storage *storage = &fstorage->storage;
	struct stat st;
	unsigned int prefix_len;
	const char *tmp_fname = NULL;
	string_t *path;
	int fd;

	path = t_str_new(256);
	str_append(path, fstorage->path);
	str_append(path, "/tmp/");
	prefix_len = str_len(path);

	for (;;) {
		tmp_fname = sieve_generate_tmp_filename(scriptname);
		str_truncate(path, prefix_len);
		str_append(path, tmp_fname);

		/* stat() first to see if it exists. pretty much the only
		   possibility of that happening is if time had moved
		   backwards, but even then it's highly unlikely. */
		if (stat(str_c(path), &st) == 0) {
			/* try another file name */
		} else if (errno != ENOENT) {
			switch ( errno ) {
			case EACCES:
				sieve_storage_set_critical(storage, "save: %s",
					eacces_error_get("stat", fstorage->path));
				break;
			default:
				sieve_storage_set_critical(storage, "save: "
					"stat(%s) failed: %m", str_c(path));
				break;
			}
			return -1;
		} else {
			/* doesn't exist */
			mode_t old_mask = umask(0777 & ~(fstorage->file_create_mode));
			fd = open(str_c(path),
				O_WRONLY | O_CREAT | O_TRUNC | O_EXCL, 0777);
			umask(old_mask);

			if (fd != -1 || errno != EEXIST)
				break;
			/* race condition between stat() and open().
				highly unlikely. */
		}
	}

	*fpath_r = str_c(path);
	if (fd == -1) {
		if (ENOQUOTA(errno)) {
			sieve_storage_set_error(storage,
				SIEVE_ERROR_NO_QUOTA,
				"Not enough disk quota");
		} else {
			switch ( errno ) {
			case EACCES:
				sieve_storage_set_critical(storage, "save: %s",
					eacces_error_get("open", fstorage->path));
				break;
			default:
				sieve_storage_set_critical(storage, "save: "
					"open(%s) failed: %m", str_c(path));
				break;
			}
		}
	}

	return fd;
}

static int sieve_file_storage_script_move
(struct sieve_file_save_context *fsctx, const char *dst)
{
	struct sieve_storage_save_context *sctx = &fsctx->context;
	struct sieve_storage *storage = sctx->storage;
	int result = 0;

	T_BEGIN {

		/* Using rename() to ensure existing files are replaced
		 * without conflicts with other processes using the same
		 * file. The kernel wont fully delete the original until
		 * all processes have closed the file.
		 */
		if (rename(fsctx->tmp_path, dst) == 0)
			result = 0;
		else {
			result = -1;
			if ( ENOQUOTA(errno) ) {
				sieve_storage_set_error(storage,
					SIEVE_ERROR_NO_QUOTA,
					"Not enough disk quota");
			} else if ( errno == EACCES ) {
				sieve_storage_set_critical(storage, "save: "
					"Failed to save Sieve script: "
					"%s", eacces_error_get("rename", dst));
			} else {
				sieve_storage_set_critical(storage, "save: "
					"rename(%s, %s) failed: %m", fsctx->tmp_path, dst);
			}
		}

		/* Always destroy temp file */
		if (unlink(fsctx->tmp_path) < 0 && errno != ENOENT) {
			sieve_storage_sys_warning(storage, "save: "
				"unlink(%s) failed: %m", fsctx->tmp_path);
		}
	} T_END;

	return result;
}

struct sieve_storage_save_context *
sieve_file_storage_save_init(struct sieve_storage *storage,
	const char *scriptname, struct istream *input)
{
	struct sieve_file_storage *fstorage =
		(struct sieve_file_storage *)storage;
	struct sieve_file_save_context *fsctx;
	pool_t pool;
	const char *path;
	int fd;

	if ( sieve_file_storage_pre_modify(storage) < 0 )
		return NULL;

	if ( scriptname != NULL ) {
		/* Prevent overwriting the active script link when it resides in the
		 * sieve storage directory.
		 */
		i_assert( fstorage->link_path != NULL );
		if ( *(fstorage->link_path) == '\0' ) {
			const char *svext;
			size_t namelen;

			svext = strrchr(fstorage->active_fname, '.');
			namelen = svext - fstorage->active_fname;
			if ( svext != NULL && str_begins(svext+1, "sieve") &&
			     strlen(scriptname) == namelen &&
			     str_begins(fstorage->active_fname, scriptname) )
			{
				sieve_storage_set_error(storage,
					SIEVE_ERROR_BAD_PARAMS,
					"Script name `%s' is reserved for internal use.",
					scriptname);
				return NULL;
			}
		}
	}

	T_BEGIN {
		fd = sieve_file_storage_create_tmp(fstorage, scriptname, &path);
		if (fd == -1) {
			fsctx = NULL;
		} else {
			pool = pool_alloconly_create("sieve_file_save_context", 1024);
			fsctx = p_new(pool, struct sieve_file_save_context, 1);
			fsctx->context.scriptname = p_strdup(pool, scriptname);
			fsctx->context.input = input;
			fsctx->context.pool = pool;
			fsctx->fd = fd;
			fsctx->output = o_stream_create_fd(fsctx->fd, 0);
			fsctx->tmp_path = p_strdup(pool, path);
		}
	} T_END;

	return &fsctx->context;
}

int sieve_file_storage_save_continue
(struct sieve_storage_save_context *sctx)
{
	struct sieve_file_save_context *fsctx =
		(struct sieve_file_save_context *)sctx;

	switch (o_stream_send_istream(fsctx->output, sctx->input)) {
	case OSTREAM_SEND_ISTREAM_RESULT_FINISHED:
	case OSTREAM_SEND_ISTREAM_RESULT_WAIT_INPUT:
		return 0;
	case OSTREAM_SEND_ISTREAM_RESULT_WAIT_OUTPUT:
		i_unreached();
	case OSTREAM_SEND_ISTREAM_RESULT_ERROR_INPUT:
		sieve_storage_set_critical(sctx->storage,
			"save: read(%s) failed: %s",
			i_stream_get_name(sctx->input),
			i_stream_get_error(sctx->input));
		return -1;
	case OSTREAM_SEND_ISTREAM_RESULT_ERROR_OUTPUT:
		sieve_storage_set_critical(sctx->storage,
			"save: write(%s) failed: %s", fsctx->tmp_path,
			o_stream_get_error(fsctx->output));
		return -1;
	}
	return 0;
}

int sieve_file_storage_save_finish
(struct sieve_storage_save_context *sctx)
{
	struct sieve_file_save_context *fsctx =
		(struct sieve_file_save_context *)sctx;
	struct sieve_storage *storage = sctx->storage;
	int output_errno;

	if ( sctx->failed && fsctx->fd == -1 ) {
		/* tmp file creation failed */
		return -1;
	}

	T_BEGIN {
		output_errno = fsctx->output->stream_errno;
		o_stream_destroy(&fsctx->output);

		if ( fsync(fsctx->fd) < 0 ) {
			sieve_storage_set_critical(storage, "save: "
				"fsync(%s) failed: %m", fsctx->tmp_path);
			sctx->failed = TRUE;
		}
		if ( close(fsctx->fd) < 0 ) {
			sieve_storage_set_critical(storage, "save: "
				"close(%s) failed: %m", fsctx->tmp_path);
			sctx->failed = TRUE;
		}
		fsctx->fd = -1;

		if ( sctx->failed ) {
			/* delete the tmp file */
			if (unlink(fsctx->tmp_path) < 0 && errno != ENOENT) {
				sieve_storage_sys_warning(storage, "save: "
					"unlink(%s) failed: %m", fsctx->tmp_path);
			}

			fsctx->tmp_path = NULL;
			
			errno = output_errno;
			if ( ENOQUOTA(errno) ) {
				sieve_storage_set_error(storage,
					SIEVE_ERROR_NO_QUOTA,
					"Not enough disk quota");
			} else if ( errno != 0 ) {
				sieve_storage_set_critical(storage, "save: "
					"write(%s) failed: %m", fsctx->tmp_path);
			}
		}
	} T_END;

	return ( sctx->failed ? -1 : 0 );
}

struct sieve_script *sieve_file_storage_save_get_tempscript
(struct sieve_storage_save_context *sctx)
{
	struct sieve_file_save_context *fsctx =
		(struct sieve_file_save_context *)sctx;
	struct sieve_file_storage *fstorage = 
		(struct sieve_file_storage *)sctx->storage;
	struct sieve_file_script *tmpscript;
	enum sieve_error error;
	const char *scriptname;

	if (sctx->failed)
		return NULL;

	if ( sctx->scriptobject != NULL )
		return sctx->scriptobject;

	scriptname =
		( sctx->scriptname == NULL ? "" : sctx->scriptname );
	tmpscript = sieve_file_script_open_from_path
		(fstorage, fsctx->tmp_path, scriptname, &error);

	if ( tmpscript == NULL ) {
		if ( error == SIEVE_ERROR_NOT_FOUND ) {
			sieve_storage_set_critical(sctx->storage, "save: "
				"Temporary script file `%s' got lost, "
				"which should not happen (possibly deleted externally).",
				fsctx->tmp_path);
		} else {
			sieve_storage_set_critical(sctx->storage, "save: "
				"Failed to open temporary script file `%s'",
				fsctx->tmp_path);
		}
		return NULL;
	}

	return &tmpscript->script;
}

static void sieve_file_storage_update_mtime
(struct sieve_storage *storage, const char *path, time_t mtime)
{
	struct utimbuf times = { .actime = mtime, .modtime = mtime };

	if ( utime(path, &times) < 0 ) {
		switch ( errno ) {	
		case ENOENT:
			break;
		case EACCES:
			sieve_storage_sys_error(storage, "save: "
				"%s", eacces_error_get("utime", path));
			break;
		default:
			sieve_storage_sys_error(storage, "save: "
				"utime(%s) failed: %m", path);
		}
	}
}

int sieve_file_storage_save_commit
(struct sieve_storage_save_context *sctx)
{
	struct sieve_file_save_context *fsctx =
		(struct sieve_file_save_context *)sctx;
	struct sieve_storage *storage = sctx->storage;
	struct sieve_file_storage *fstorage =
		(struct sieve_file_storage *)sctx->storage;
	const char *dest_path;
	bool failed = FALSE;

	i_assert(fsctx->output == NULL);

	T_BEGIN {
		dest_path = t_strconcat(fstorage->path, "/",
			sieve_script_file_from_name(sctx->scriptname), NULL);

		failed = ( sieve_file_storage_script_move(fsctx, dest_path) < 0 );
		if ( sctx->mtime != (time_t)-1 )
			sieve_file_storage_update_mtime(storage, dest_path, sctx->mtime);
	} T_END;

	pool_unref(&sctx->pool);
	
	return ( failed ? -1 : 0 );
}

void sieve_file_storage_save_cancel(struct sieve_storage_save_context *sctx)
{
	struct sieve_file_save_context *fsctx =
		(struct sieve_file_save_context *)sctx;
	struct sieve_storage *storage = sctx->storage;

	if (fsctx->tmp_path != NULL &&
		unlink(fsctx->tmp_path) < 0 && errno != ENOENT) {
		sieve_storage_sys_warning(storage, "save: "
			"unlink(%s) failed: %m", fsctx->tmp_path);
	}

	i_assert(fsctx->output == NULL);
}

static int
sieve_file_storage_save_to(struct sieve_file_storage *fstorage,
	string_t *temp_path, struct istream *input,
	const char *target)
{
	struct sieve_storage *storage = &fstorage->storage;
	struct ostream *output;
	int fd;

	// FIXME: move this to base class
	// FIXME: use io_stream_temp

	fd = safe_mkstemp_hostpid
		(temp_path, fstorage->file_create_mode, (uid_t)-1, (gid_t)-1);
	if ( fd < 0 ) {
		if ( errno == EACCES ) {
			sieve_storage_set_critical(storage,
				"Failed to create temporary file: %s",
				eacces_error_get_creating("open", str_c(temp_path)));
		} else {
			sieve_storage_set_critical(storage,
				"Failed to create temporary file: open(%s) failed: %m",
				str_c(temp_path));
		}
		return -1;
	}

	output = o_stream_create_fd(fd, 0);
	switch ( o_stream_send_istream(output, input) ) {
	case OSTREAM_SEND_ISTREAM_RESULT_FINISHED:
		break;
	case OSTREAM_SEND_ISTREAM_RESULT_WAIT_INPUT:
	case OSTREAM_SEND_ISTREAM_RESULT_WAIT_OUTPUT:
		i_unreached();
	case OSTREAM_SEND_ISTREAM_RESULT_ERROR_INPUT:
		sieve_storage_set_critical(storage,
			"read(%s) failed: %s", i_stream_get_name(input),
			i_stream_get_error(input));
		o_stream_destroy(&output);
		i_unlink(str_c(temp_path));
		return -1;
	case OSTREAM_SEND_ISTREAM_RESULT_ERROR_OUTPUT:
		sieve_storage_set_critical(storage,
			"write(%s) failed: %s", str_c(temp_path),
			o_stream_get_error(output));
		o_stream_destroy(&output);
		i_unlink(str_c(temp_path));
		return -1;
	}
	o_stream_destroy(&output);

	if ( rename(str_c(temp_path), target) < 0 ) {
		if ( ENOQUOTA(errno) ) {
			sieve_storage_set_error(storage,
				SIEVE_ERROR_NO_QUOTA,
				"Not enough disk quota");
		} else if ( errno == EACCES ) {
			sieve_storage_set_critical(storage,
				"%s", eacces_error_get("rename", target));
		} else {
			sieve_storage_set_critical(storage,
				"rename(%s, %s) failed: %m",
				str_c(temp_path), target);
		}
		i_unlink(str_c(temp_path));
	}
	return 0;
}

int sieve_file_storage_save_as
(struct sieve_storage *storage, struct istream *input,
	const char *name)
{
	struct sieve_file_storage *fstorage =
		(struct sieve_file_storage *)storage;
	string_t *temp_path;
	const char *dest_path;

	temp_path = t_str_new(256);
	str_append(temp_path, fstorage->path);
	str_append(temp_path, "/tmp/");
	str_append(temp_path, sieve_script_file_from_name(name));
	str_append_c(temp_path, '.');

	dest_path = t_strconcat(fstorage->path, "/",
		sieve_script_file_from_name(name), NULL);

	return sieve_file_storage_save_to
		(fstorage, temp_path, input, dest_path);
}

int sieve_file_storage_save_as_active
(struct sieve_storage *storage, struct istream *input,
	time_t mtime)
{
	struct sieve_file_storage *fstorage =
		(struct sieve_file_storage *)storage;
	string_t *temp_path;

	temp_path = t_str_new(256);
	str_append(temp_path, fstorage->active_path);
	str_append_c(temp_path, '.');

	if ( sieve_file_storage_save_to
		(fstorage, temp_path, input, fstorage->active_path) < 0 )
		return -1;

	sieve_file_storage_update_mtime
		(storage, fstorage->active_path, mtime);
	return 0;
}

