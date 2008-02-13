#include <stdio.h>

#include "lib.h"
#include "str.h"
#include "ostream.h"

#include "sieve-error-private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#define CRITICAL_MSG \
	"internal error occurred: refer to server log for more information."
#define CRITICAL_MSG_STAMP CRITICAL_MSG " [%Y-%m-%d %H:%M:%S]"

void sieve_verror
	(struct sieve_error_handler *ehandler, const char *location, 
		const char *fmt, va_list args)
{
	if ( ehandler == NULL ) return;
	
	if ( ehandler->log_master )
		i_error("sieve: %s: %s", location, t_strdup_vprintf(fmt, args));

	ehandler->verror(ehandler, location, fmt, args);
	ehandler->errors++;
}

void sieve_vwarning
	(struct sieve_error_handler *ehandler, const char *location, 
		const char *fmt, va_list args)
{
	if ( ehandler == NULL ) return;

	if ( ehandler->log_master )
		i_warning("sieve: %s: %s", location, t_strdup_vprintf(fmt, args));
		
	ehandler->vwarning(ehandler, location, fmt, args);
	ehandler->warnings++;
}

void sieve_vinfo
	(struct sieve_error_handler *ehandler, const char *location, 
		const char *fmt, va_list args)
{
	if ( ehandler == NULL ) return;

	if ( ehandler->log_master )
		i_info("sieve: %s: %s", location, t_strdup_vprintf(fmt, args));
	
	if ( ehandler->log_info )	
		ehandler->vinfo(ehandler, location, fmt, args);
}

void sieve_vcritical
	(struct sieve_error_handler *ehandler, const char *location, 
		const char *fmt, va_list args)
{
	char str[256];
	struct tm *tm; 
	
	tm = localtime(&ioloop_time);
	
	i_error("sieve: error: %s: %s", location, t_strdup_vprintf(fmt, args));
	
	if ( ehandler == NULL ) return;
	
	sieve_error(ehandler, location, "%s", 
		strftime(str, sizeof(str), CRITICAL_MSG_STAMP, tm) > 0 ? 
			str : CRITICAL_MSG );	
}

unsigned int sieve_get_errors(struct sieve_error_handler *ehandler) {
	if ( ehandler == NULL ) return 0;
	
	return ehandler->errors;
}

unsigned int sieve_get_warnings(struct sieve_error_handler *ehandler) {
	if ( ehandler == NULL ) return 0;

	return ehandler->errors;
}

void sieve_error_handler_accept_infolog
	(struct sieve_error_handler *ehandler, bool enable)
{
	ehandler->log_info = enable;	
}

void sieve_error_handler_copy_masterlog
	(struct sieve_error_handler *ehandler, bool enable)
{
	ehandler->log_master = enable;
}

void sieve_error_handler_init
	(struct sieve_error_handler *ehandler, pool_t pool)
{
	ehandler->pool = pool;
	ehandler->refcount = 1;
	ehandler->errors = 0;
	ehandler->warnings = 0;
}

void sieve_error_handler_ref(struct sieve_error_handler *ehandler)
{
	if ( ehandler == NULL ) return;

	ehandler->refcount++;
}

void sieve_error_handler_unref(struct sieve_error_handler **ehandler)
{
	if ( *ehandler == NULL ) return;

    i_assert((*ehandler)->refcount > 0);

    if (--(*ehandler)->refcount != 0)
        return;

	if ( (*ehandler)->free != NULL )
		(*ehandler)->free(*ehandler);

	pool_unref(&((*ehandler)->pool));

	*ehandler = NULL;
}

/* Output errors directly to stderror (merge this with logfile below?) */

static void sieve_stderr_verror
(struct sieve_error_handler *ehandler ATTR_UNUSED, const char *location, 
	const char *fmt, va_list args) 
{
	fprintf(stderr, "%s: error: %s.\n", location, t_strdup_vprintf(fmt, args));
}

static void sieve_stderr_vwarning
(struct sieve_error_handler *ehandler ATTR_UNUSED, const char *location, 
	const char *fmt, va_list args) 
{
	fprintf(stderr, "%s: warning: %s.\n", location, t_strdup_vprintf(fmt, args));
}

static void sieve_stderr_vinfo
(struct sieve_error_handler *ehandler ATTR_UNUSED, const char *location, 
	const char *fmt, va_list args) 
{
	fprintf(stderr, "%s: info: %s.\n", location, t_strdup_vprintf(fmt, args));
}

struct sieve_error_handler *sieve_stderr_ehandler_create( void ) 
{
	pool_t pool;
	struct sieve_error_handler *ehandler;
	
	/* Pool is not strictly necessary, but other handler types will need a pool,
	 * so this one will have one too.
	 */
	pool = pool_alloconly_create
		("stderr_error_handler", sizeof(struct sieve_error_handler));
//		("stderr_error_handler", 128);	
	ehandler = p_new(pool, struct sieve_error_handler, 1);
	sieve_error_handler_init(ehandler, pool);

	ehandler->verror = sieve_stderr_verror;
	ehandler->vwarning = sieve_stderr_vwarning;
	ehandler->vinfo = sieve_stderr_vinfo;
	
	return ehandler;	
}

/* Output errors to a string buffer */

struct sieve_strbuf_ehandler {
    struct sieve_error_handler handler;

	string_t *errors;
};

static void sieve_strbuf_verror
(struct sieve_error_handler *ehandler, const char *location,
    const char *fmt, va_list args)
{
    struct sieve_strbuf_ehandler *handler =
        (struct sieve_strbuf_ehandler *) ehandler;

	str_printfa(handler->errors, "%s: error: ", location);
	str_vprintfa(handler->errors, fmt, args);
	str_append(handler->errors, ".\n");
}

static void sieve_strbuf_vwarning
(struct sieve_error_handler *ehandler, const char *location,
    const char *fmt, va_list args)
{
    struct sieve_strbuf_ehandler *handler =
        (struct sieve_strbuf_ehandler *) ehandler;

	str_printfa(handler->errors, "%s: warning: ", location);
    str_vprintfa(handler->errors, fmt, args);
    str_append(handler->errors, ".\n");
}

static void sieve_strbuf_vinfo
(struct sieve_error_handler *ehandler, const char *location,
    const char *fmt, va_list args)
{
    struct sieve_strbuf_ehandler *handler =
        (struct sieve_strbuf_ehandler *) ehandler;
	
	str_printfa(handler->errors, "%s: info: ", location);
    str_vprintfa(handler->errors, fmt, args);
    str_append(handler->errors, ".\n");
}

struct sieve_error_handler *sieve_strbuf_ehandler_create
(string_t *strbuf)
{
    pool_t pool;
    struct sieve_strbuf_ehandler *ehandler;

    pool = pool_alloconly_create("strbuf_error_handler", 256);
    ehandler = p_new(pool, struct sieve_strbuf_ehandler, 1);
	ehandler->errors = strbuf;
    
	sieve_error_handler_init(&ehandler->handler, pool);

    ehandler->handler.verror = sieve_strbuf_verror;
    ehandler->handler.vwarning = sieve_strbuf_vwarning;
    ehandler->handler.vinfo = sieve_strbuf_vinfo;

    return &(ehandler->handler);
}

/* Output errors to a log file */

struct sieve_logfile_ehandler {
	struct sieve_error_handler handler;
	
	const char *logfile;
	bool started;
	int fd;
	struct ostream *stream;
};

static void sieve_logfile_vprintf
(struct sieve_logfile_ehandler *ehandler, const char *location, 
	const char *prefix,	const char *fmt, va_list args) 
{
	string_t *outbuf;
	
	if ( ehandler->stream == NULL ) return;
	
	T_BEGIN {
		outbuf = t_str_new(256);
		str_printfa(outbuf, "%s: %s: ", location, prefix);	
		str_vprintfa(outbuf, fmt, args);
		str_append(outbuf, ".\n");
	
		o_stream_send(ehandler->stream, str_data(outbuf), str_len(outbuf));
	} T_END;
}

inline static void sieve_logfile_printf
(struct sieve_logfile_ehandler *ehandler, const char *location, const char *prefix,
	const char *fmt, ...) 
{
	va_list args;
	va_start(args, fmt);
	
	sieve_logfile_vprintf(ehandler, location, prefix, fmt, args);
	
	va_end(args);
}

static void sieve_logfile_start(struct sieve_logfile_ehandler *ehandler)
{
	int fd;
	struct ostream *ostream = NULL;
	struct tm *tm;
	char buf[256];
	time_t now;

	fd = open(ehandler->logfile, O_CREAT | O_APPEND | O_WRONLY, 0600);
	if (fd == -1) {
		i_error("sieve: Failed to open logfile %s (logging to STDERR): %m", 
			ehandler->logfile);
		fd = STDERR_FILENO;
	}
	/* else
		fd_close_on_exec(fd, TRUE); Necessary? */

	ostream = o_stream_create_fd(fd, 0, FALSE);
	if ( ostream == NULL ) {
		/* Can't we do anything else in this most awkward situation? */
		i_error("sieve: Failed to open log stream on open file %s. "
			"Nothing will be logged.", 
			ehandler->logfile);
	} 

	ehandler->fd = fd;
	ehandler->stream = ostream;
	ehandler->started = TRUE;
	
	if ( ostream != NULL ) {
		now = time(NULL);	
		tm = localtime(&now);

		if (strftime(buf, sizeof(buf), "%b %d %H:%M:%S", tm) > 0) {
			sieve_logfile_printf(ehandler, "sieve", "info",
				"started log at %s", buf);
		}
	}
}

static void sieve_logfile_verror
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, va_list args) 
{
	struct sieve_logfile_ehandler *handler = 
		(struct sieve_logfile_ehandler *) ehandler;

	if ( !handler->started ) sieve_logfile_start(handler);	

	sieve_logfile_vprintf(handler, location, "error", fmt, args);
}

static void sieve_logfile_vwarning
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, va_list args) 
{
	struct sieve_logfile_ehandler *handler = 
		(struct sieve_logfile_ehandler *) ehandler;

	if ( !handler->started ) sieve_logfile_start(handler);	

	sieve_logfile_vprintf(handler, location, "warning", fmt, args);
}

static void sieve_logfile_vinfo
(struct sieve_error_handler *ehandler, const char *location, 
	const char *fmt, va_list args) 
{
	struct sieve_logfile_ehandler *handler = 
		(struct sieve_logfile_ehandler *) ehandler;

	if ( !handler->started ) sieve_logfile_start(handler);	

	sieve_logfile_vprintf(handler, location, "info", fmt, args);
}


static void sieve_logfile_free
(struct sieve_error_handler *ehandler)
{
	struct sieve_logfile_ehandler *handler = 
		(struct sieve_logfile_ehandler *) ehandler;
		
	if ( handler->stream != NULL ) {
		o_stream_destroy(&(handler->stream));
		if ( handler->fd != STDERR_FILENO )
			close(handler->fd);
	}
}

struct sieve_error_handler *sieve_logfile_ehandler_create(const char *logfile) 
{
	pool_t pool;
	struct sieve_logfile_ehandler *ehandler;
	
	pool = pool_alloconly_create("logfile_error_handler", 256);	
	ehandler = p_new(pool, struct sieve_logfile_ehandler, 1);
	sieve_error_handler_init(&ehandler->handler, pool);

	ehandler->handler.verror = sieve_logfile_verror;
	ehandler->handler.vwarning = sieve_logfile_vwarning;
	ehandler->handler.vinfo = sieve_logfile_vinfo;
	ehandler->handler.free = sieve_logfile_free;
	
	/* Don't open logfile until something is actually logged. 
	 * Let's not pullute the sieve directory with useless logfiles.
	 */
	ehandler->logfile = p_strdup(pool, logfile);
	ehandler->started = FALSE;
	ehandler->stream = NULL;
	ehandler->fd = -1;
	
	return &(ehandler->handler);	
}

