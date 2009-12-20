/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "compat.h"
#include "unichar.h"
#include "array.h"
#include "istream.h"
#include "eacces-error.h"

#include "sieve-common.h"
#include "sieve-error.h"

#include "sieve-script-private.h"

#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

/*
 * Configuration
 */
 
#define SIEVE_READ_BLOCK_SIZE (1024*8)

/*
 * Script name
 */

bool sieve_script_name_is_valid(const char *scriptname)
{
	ARRAY_TYPE(unichars) uni_name;
	unsigned int count, i;
	const unichar_t *name_chars;

	/* Intialize array for unicode characters */
	t_array_init(&uni_name, strlen(scriptname)* 4);

	/* Convert UTF-8 to UCS4/UTF-32 */
	if ( uni_utf8_to_ucs4(scriptname, &uni_name) < 0 )
		return FALSE;

	/* Scan name for invalid characters */
	name_chars = array_get(&uni_name, &count);
	for ( i = 0; i < count; i++ ) {

		/* 0000-001F; [CONTROL CHARACTERS] */
		if ( name_chars[i] <= 0x001f )
			return FALSE;
		
		/* 002F; SLASH */
		if ( name_chars[i] == 0x002f )
			return FALSE;

		/* 007F; DELETE */
		if ( name_chars[i] == 0x007f )
			return FALSE;

		/* 0080-009F; [CONTROL CHARACTERS] */
		if ( name_chars[i] >= 0x0080 && name_chars[i] <= 0x009f )
			return FALSE;

		/* 2028; LINE SEPARATOR */
		/* 2029; PARAGRAPH SEPARATOR */
		if ( name_chars[i] == 0x2028 || name_chars[i] == 0x2029 )
			return FALSE;
	}

	return TRUE;
}

/*
 * Filename to name/name to filename
 */

static inline const char *_sieve_scriptfile_get_basename(const char *filename)
{
	const char *ext;

	/* Extract the script name */
	ext = strrchr(filename, '.');
	if ( ext == NULL || ext == filename || strncmp(ext,".sieve",6) != 0 )
		return filename;
	
	return t_strdup_until(filename, ext);	
}

bool sieve_script_file_has_extension(const char *filename)
{
	const char *ext;

 	/* See if it ends in .sieve already */
	ext = strrchr(filename, '.');
	if ( ext == NULL || ext == filename || strncmp(ext,".sieve",6) != 0 )
		return FALSE;

	return TRUE;
}

static inline const char *_sieve_scriptfile_from_name(const char *name)
{
	if ( !sieve_script_file_has_extension(name) )
		return t_strconcat(name, ".sieve", NULL);

	return name;
}


/* 
 * Script object 
 */
 
struct sieve_script *sieve_script_init
(struct sieve_script *script, struct sieve_instance *svinst, 
	const char *path, const char *name, struct sieve_error_handler *ehandler, 
	bool *exists_r)
{
	int ret;
	pool_t pool;
	struct stat st;
	struct stat lnk_st;
	const char *filename, *dirpath, *basename, *binpath;

	if ( exists_r != NULL )
		*exists_r = TRUE;

	T_BEGIN {

		/* Extract filename from path */

		filename = strrchr(path, '/');
		if ( filename == NULL ) {
			dirpath = "";
			filename = path;
		} else {
			dirpath = t_strdup_until(path, filename);
			filename++;
		}

		basename = _sieve_scriptfile_get_basename(filename);

		if ( *dirpath == '\0' )
			binpath = t_strconcat(basename, ".svbin", NULL);
		else
			binpath = t_strconcat(dirpath, "/", basename, ".svbin", NULL);
				
		if ( name == NULL ) {
			name = basename; 
		} else if ( *name == '\0' ) {
			name = NULL;
		} else {
			basename = name;
		}
			
		/* First obtain stat data from the system */
		
		if ( (ret=lstat(path, &st)) < 0 ) {
			if ( errno == ENOENT ) {
				if ( exists_r == NULL ) 
					sieve_error(ehandler, basename, "sieve script does not exist");
				else
					*exists_r = FALSE;
			} else
				sieve_critical(ehandler, basename, 
					"failed to stat sieve script: lstat(%s) failed: %m", path);

			script = NULL;
			ret = 1;

		} else {
			/* Record stat information from the symlink */
			lnk_st = st;

			/* Only create/init the object if it stat()s without problems */
			if (S_ISLNK(st.st_mode)) {
				if ( (ret=stat(path, &st)) < 0 ) { 
					if ( errno == ENOENT ) {
						if ( exists_r == NULL )
							sieve_error(ehandler, basename, "sieve script does not exist");
						else
							*exists_r = FALSE;
					} else
						sieve_critical(ehandler, basename, 
							"failed to stat sieve script: stat(%s) failed: %m", path);

					script = NULL;	
					ret = 1;
				}
			}

			if ( ret == 0 && !S_ISREG(st.st_mode) ) {
				sieve_critical(ehandler, basename, 
					"sieve script file '%s' is not a regular file.", path);
				script = NULL;
				ret = 1;
			} 
		}

		if ( ret <= 0 ) {
			if ( script == NULL ) {
				pool = pool_alloconly_create("sieve_script", 1024);
				script = p_new(pool, struct sieve_script, 1);
				script->pool = pool;
			} else 
				pool = script->pool;
		
			script->refcount = 1;
			script->svinst = svinst;

			script->ehandler = ehandler;
			sieve_error_handler_ref(ehandler);
		
			script->st = st;
			script->lnk_st = lnk_st;
			script->path = p_strdup(pool, path);
			script->filename = p_strdup(pool, filename);
			script->dirpath = p_strdup(pool, dirpath);
			script->binpath = p_strdup(pool, binpath);
			script->basename = p_strdup(pool, basename);

			if ( name != NULL )
				script->name = p_strdup(pool, name);
			else
				script->name = NULL;
		}
	} T_END;	

	return script;
}

struct sieve_script *sieve_script_create
(struct sieve_instance *svinst, const char *path, const char *name, 
	struct sieve_error_handler *ehandler, bool *exists_r)
{
	return sieve_script_init(NULL, svinst, path, name, ehandler, exists_r);
}

struct sieve_script *sieve_script_create_in_directory
(struct sieve_instance *svinst, const char *dirpath, const char *name,
	struct sieve_error_handler *ehandler, bool *exists_r)
{
	const char *path;

	if ( dirpath[strlen(dirpath)-1] == '/' )
		path = t_strconcat(dirpath, 
			_sieve_scriptfile_from_name(name), NULL);
	else
		path = t_strconcat(dirpath, "/",
			_sieve_scriptfile_from_name(name), NULL);

	return sieve_script_init(NULL, svinst, path, name, ehandler, exists_r);
}

void sieve_script_ref(struct sieve_script *script)
{
	script->refcount++;
}

void sieve_script_unref(struct sieve_script **script)
{
	i_assert((*script)->refcount > 0);

	if (--(*script)->refcount != 0)
		return;

	if ( (*script)->stream != NULL )
		i_stream_destroy(&(*script)->stream);

	sieve_error_handler_unref(&(*script)->ehandler);

	pool_unref(&(*script)->pool);

	*script = NULL;
}

/* 
 * Accessors 
 */

const char *sieve_script_name(const struct sieve_script *script)
{
	return script->name;
}

const char *sieve_script_filename(const struct sieve_script *script)
{
	return script->filename;
}

const char *sieve_script_path(const struct sieve_script *script)
{
	return script->path;
}

const char *sieve_script_dirpath(const struct sieve_script *script)
{
	return script->dirpath;
}

const char *sieve_script_binpath(const struct sieve_script *script)
{
	return script->binpath;
}

mode_t sieve_script_permissions(const struct sieve_script *script)
{
	return script->st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
}

struct sieve_instance *sieve_script_svinst(const struct sieve_script *script)
{
	return script->svinst;
}

/* 
 * Stream manageement 
 */

struct istream *sieve_script_open
(struct sieve_script *script, bool *deleted_r)
{
	int fd;
	struct stat st;
	struct istream *result;

	if ( deleted_r != NULL )
		*deleted_r = FALSE;

	if ( (fd=open(script->path, O_RDONLY)) < 0 ) {
		if ( errno == ENOENT ) {
			if ( deleted_r == NULL ) 
				/* Not supposed to occur, create() does stat already */
				sieve_error(script->ehandler, script->basename, 
					"sieve script does not exist");
			else 
				*deleted_r = TRUE;
		} else if ( errno == EACCES ) {
			sieve_critical(script->ehandler, script->path,
				"failed to open sieve script: %s",
				eacces_error_get("open", script->path));
		} else {
			sieve_critical(script->ehandler, script->path, 
				"failed to open sieve script: open(%s) failed: %m", script->path);
		}
		return NULL;
	}	
	
	if ( fstat(fd, &st) != 0 ) {
		sieve_critical(script->ehandler, script->path, 
			"failed to open sieve script: fstat(fd=%s) failed: %m", script->path);
		result = NULL;
	} else {
		/* Re-check the file type just to be sure */
		if ( !S_ISREG(st.st_mode) ) {
			sieve_critical(script->ehandler, script->path,
				"sieve script file '%s' is not a regular file", script->path);
			result = NULL;
		} else {
			result = script->stream = 
				i_stream_create_fd(fd, SIEVE_READ_BLOCK_SIZE, TRUE);
			script->st = script->lnk_st = st;
		}
	}

	if ( result == NULL ) {
		/* Something went wrong, close the fd */
		if ( close(fd) != 0 ) {
			sieve_sys_error(
				"failed to close sieve script: close(fd=%s) failed: %m", 
				script->path);
		}
	}
	
	return result;
}

void sieve_script_close(struct sieve_script *script)
{
	i_stream_destroy(&script->stream);
}

uoff_t sieve_script_get_size(const struct sieve_script *script)
{
	return script->st.st_size;
}

/* 
 * Comparison 
 */

int sieve_script_cmp
(const struct sieve_script *script1, const struct sieve_script *script2)
{
	if ( script1 == NULL || script2 == NULL ) 
		return -1;	

	return ( script1->st.st_ino == script2->st.st_ino ) ? 0 : -1;
}

unsigned int sieve_script_hash(const struct sieve_script *script)
{	
	return (unsigned int) script->st.st_ino;
}

bool sieve_script_newer
(const struct sieve_script *script, time_t time)
{
	return ( script->st.st_mtime > time || script->lnk_st.st_mtime > time );
}
