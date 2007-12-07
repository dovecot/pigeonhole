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
	
	/* Parameters */
	const char *name;
	const char *path;	

	/* Stream */
	int fd; /* FIXME: we could use the stream's autoclose facility */
	struct istream *stream;
};

/* Script object */

struct sieve_script *sieve_script_create
	(const char *path, const char *name)
{
	pool_t pool;
	struct sieve_script *script;
	
	pool = pool_alloconly_create("sieve_script", 1024);
	script = p_new(pool, struct sieve_script, 1);
	script->pool = pool;
	script->refcount = 1;
	
	script->path = p_strdup(pool, path);
		
	if ( name == NULL || *name == '\0' ) {
		const char *filename, *ext;
	
		T_FRAME(
			/* Extract filename from path */
			filename = strrchr(path, '/');
			if ( filename == NULL )
				filename = path;
			else
				filename++;

			/* Extract the script name */
		  ext = strrchr(filename, '.');
		  if ( ext == NULL || ext == filename || strncmp(ext,".sieve",6) != 0 )
				script->name = p_strdup(pool, filename);
		  else
		  	script->name = p_strdup_until(pool, filename, ext);
		);
	} else {
		script->name = p_strdup(pool, name);
	}
	
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

struct istream *sieve_script_open(struct sieve_script *script, 
	struct sieve_error_handler *ehandler)
{
	int fd;

	if ( (fd=open(script->path, O_RDONLY)) < 0 ) {
		if ( errno == ENOENT )
			sieve_error(ehandler, script->path, "sieve script '%s' does not exist",
				script->name);
		else 
			sieve_critical(ehandler, script->path, "failed to open sieve script: %m");
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
	return ( strcmp(script1->path, script2->path) == 0 );
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


