#include "lib.h"
#include "compat.h"
#include "istream.h"

#include "sieve-common.h"
#include "sieve-error.h"

#include "sieve-script.h"

#define SIEVE_READ_BLOCK_SIZE (1024*8)

struct sieve_script {
	pool_t pool;
	unsigned int refcount;
	
	struct stat st;

	struct sieve_error_handler *ehandler;
		
	/* Parameters */
	const char *name;
	const char *dirpath;
	const char *path;	

	/* Stream */
	int fd; /* FIXME: we could use the stream's autoclose facility */
	struct istream *stream;
};

/* Script object */

struct sieve_script *sieve_script_create
	(const char *path, const char *name, struct sieve_error_handler *ehandler)
{
	pool_t pool;
	struct stat st;
	struct sieve_script *script;
	const char *filename, *dirpath;

	T_FRAME(
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
				name = filename;
			else
				name = t_strdup_until(filename, ext);
		} 
		
		/* First obtain stat data from the system */
	
		if ( stat(path, &st) < 0 ) {
			if ( errno == ENOENT )
				sieve_error(ehandler, path, "sieve script does not exist");
			else 
				sieve_critical(ehandler, path, "failed to stat sieve script: %m");
			script = NULL;
		} else {
	
			/* Only create object if it stat()s without problems */
	
			pool = pool_alloconly_create("sieve_script", 1024);
			script = p_new(pool, struct sieve_script, 1);
			script->pool = pool;
			script->refcount = 1;
			script->ehandler = ehandler;
	
			memcpy((void *) &script->st, (void *) &st, sizeof(st));
			script->path = p_strdup(pool, path);	
			script->dirpath = p_strdup(pool, dirpath);
			script->name = p_strdup(pool, name);
		}
	);

	return script;
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

	pool_unref(&(*script)->pool);

	*script = NULL;
}

/* Stream manageement */

struct istream *sieve_script_open(struct sieve_script *script)
{
	int fd;

	if ( (fd=open(script->path, O_RDONLY)) < 0 ) {
		if ( errno == ENOENT ) 
			/* Not supposed to occur, create() does stat already */
			sieve_error(script->ehandler, script->name, 
				"sieve script '%s' does not exist", script->name);
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

/* Comparison */

bool sieve_script_equals
(struct sieve_script *script1, struct sieve_script *script2)
{	
	return ( script1->st.st_ino == script2->st.st_ino );
}

/* Inline accessors */

inline const char *sieve_script_name(struct sieve_script *script)
{
	return script->name;
}

inline const char *sieve_script_path(struct sieve_script *script)
{
	return script->path;
}

inline const char *sieve_script_binpath(struct sieve_script *script)
{
	return t_strconcat(script->dirpath, "/", script->name, ".svbin", NULL);
}


