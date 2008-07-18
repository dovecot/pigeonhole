#include "lib.h"
#include "compat.h"
#include "istream.h"

#include "sieve-common.h"
#include "sieve-error.h"

#include "sieve-script-private.h"

#include <sys/stat.h>
#include <fcntl.h>

#define SIEVE_READ_BLOCK_SIZE (1024*8)

/* Script object */
struct sieve_script *sieve_script_init
(struct sieve_script *script, const char *path, const char *name, 
	struct sieve_error_handler *ehandler, bool *exists_r)
{
	int ret;
	pool_t pool;
	struct stat st;
	const char *filename, *dirpath, *basename;

	if ( exists_r != NULL )
		*exists_r = FALSE;

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
		
		if ( name == NULL || *name == '\0' ) {
			const char *ext;
			
			/* Extract the script name */
			ext = strrchr(filename, '.');
			if ( ext == NULL || ext == filename || strncmp(ext,".sieve",6) != 0 )
				basename = filename;
			else
				basename = t_strdup_until(filename, ext);
		} else {
			basename = name;
		}
			
		/* First obtain stat data from the system */
		
		if ( (ret=stat(path, &st)) != 0 && (errno != ENOENT || exists_r == NULL) ) {
			if ( errno == ENOENT ) 
				sieve_error(ehandler, basename, "sieve script does not exist");
			else
				sieve_critical(ehandler, basename, "failed to stat sieve script file '%s': %m", path);
			script = NULL;
		} else {
			/* Only create/init the object if it stat()s without problems */

			if ( ret == 0 && !S_ISREG(st.st_mode) ) {
				sieve_critical(ehandler, basename, 
					"sieve script file '%s' is not a regular file.", path);
				script = NULL;
			} else {
				if ( exists_r != NULL )
					*exists_r = ( ret == 0 );

				if ( script == NULL ) {
					pool = pool_alloconly_create("sieve_script", 1024);
					script = p_new(pool, struct sieve_script, 1);
					script->pool = pool;
				} else 
					pool = script->pool;
		
				script->refcount = 1;
				script->ehandler = ehandler;
				sieve_error_handler_ref(ehandler);
		
				script->st = st;
				script->path = p_strdup(pool, path);
				script->filename = p_strdup(pool, filename);
				script->dirpath = p_strdup(pool, dirpath);
				script->basename = p_strdup(pool, basename);
				if ( name != NULL )
					script->name = p_strdup(pool, name);
				else
					script->name = NULL;
			}
		}
	} T_END;	

	return script;
}

struct sieve_script *sieve_script_create(const char *path, const char *name,
    struct sieve_error_handler *ehandler, bool *exists_r)
{
	return sieve_script_init(NULL, path, name, ehandler, exists_r);
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

/* Stream manageement */

struct istream *sieve_script_open
(struct sieve_script *script, bool *deleted_r)
{
	int fd;

	if ( deleted_r != NULL )
		*deleted_r = FALSE;

	if ( (fd=open(script->path, O_RDONLY)) < 0 ) {
		if ( errno == ENOENT ) 
			if ( deleted_r == NULL ) 
				/* Not supposed to occur, create() does stat already */
				sieve_error(script->ehandler, script->basename, 
					"sieve script does not exist");
			else 
				*deleted_r = TRUE;
		else
			sieve_critical(script->ehandler, script->path, 
				"failed to open sieve script: %m");
		return NULL;
	}	

	script->stream = i_stream_create_fd(fd, SIEVE_READ_BLOCK_SIZE, TRUE);
	
	return script->stream;
}

void sieve_script_close(struct sieve_script *script)
{
	i_stream_destroy(&script->stream);
}

uoff_t sieve_script_get_size(struct sieve_script *script)
{
	return script->st.st_size;
}

/* Comparison */

int sieve_script_cmp
(struct sieve_script *script1, struct sieve_script *script2)
{	
	return ( script1->st.st_ino == script2->st.st_ino ) ? 0 : -1;
}

unsigned int sieve_script_hash(struct sieve_script *script)
{	
	return (unsigned int) script->st.st_ino;
}

bool sieve_script_older
(struct sieve_script *script, time_t time)
{
	return ( script->st.st_mtime < time );
}

/* Accessors */

const char *sieve_script_name(struct sieve_script *script)
{
	return script->name;
}

const char *sieve_script_filename(struct sieve_script *script)
{
	return script->filename;
}

const char *sieve_script_path(struct sieve_script *script)
{
	return script->path;
}

const char *sieve_script_binpath(struct sieve_script *script)
{
	return t_strconcat(script->dirpath, "/", script->basename, ".svbin", NULL);
}

